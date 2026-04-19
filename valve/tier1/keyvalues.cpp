//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#include <windows.h>
#include <malloc.h>

#define  stackalloc( _size ) _alloca( _size )

#include <stdlib.h>
#include "tier1/keyvalues.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlvector.h"

static const char * s_LastFileLoadingFrom = "unknown"; // just needed for error messages

#define KEYVALUES_TOKEN_SIZE	4096
static char s_pTokenBuf[KEYVALUES_TOKEN_SIZE];


#define INTERNALWRITE( pData, len ) InternalWrite( filesystem, f, pBuf, pData, len )


// a simple class to keep track of a stack of valid parsed symbols
const int MAX_ERROR_STACK = 64;
class CKeyValuesErrorStack
{
public:
	CKeyValuesErrorStack() : m_pFilename("NULL"), m_errorIndex(0), m_maxErrorIndex(0) {}

	void SetFilename( const char *pFilename )
	{
		m_pFilename = pFilename;
		m_maxErrorIndex = 0;
	}

	// entering a new keyvalues block, save state for errors
	// Not save symbols instead of pointers because the pointers can move!
	int Push( int symName )
	{
		if ( m_errorIndex < MAX_ERROR_STACK )
		{
			m_errorStack[m_errorIndex] = symName;
		}
		m_errorIndex++;
		m_maxErrorIndex = MAX( m_maxErrorIndex, (m_errorIndex-1) );
		return m_errorIndex-1;
	}

	// exiting block, error isn't in this block, remove.
	void Pop()
	{
		m_errorIndex--;
		Assert(m_errorIndex>=0);
	}

	// Allows you to keep the same stack level, but change the name as you parse peers
	void Reset( int stackLevel, int symName )
	{
		Assert( stackLevel >= 0 );
		Assert( stackLevel < m_errorIndex );
		if ( stackLevel < MAX_ERROR_STACK )
			m_errorStack[stackLevel] = symName;
	}

	// Hit an error, report it and the parsing stack for context
	void ReportError( const char *pError )
	{
	}

private:
	int		m_errorStack[MAX_ERROR_STACK];
	const char *m_pFilename;
	int		m_errorIndex;
	int		m_maxErrorIndex;
} g_KeyValuesErrorStack;


// a simple helper that creates stack entries as it goes in & out of scope
class CKeyErrorContext
{
public:
	CKeyErrorContext( KeyValues *pKv )
	{
		Init( pKv->GetNameSymbol() );
	}

	~CKeyErrorContext()
	{
		g_KeyValuesErrorStack.Pop();
	}
	CKeyErrorContext( int symName )
	{
		Init( symName );
	}
	void Reset( int symName )
	{
		g_KeyValuesErrorStack.Reset( m_stackLevel, symName );
	}
	int GetStackLevel() const
	{
		return m_stackLevel;
	}
private:
	void Init( int symName )
	{
		m_stackLevel = g_KeyValuesErrorStack.Push( symName );
	}

	int m_stackLevel;
};

// Uncomment this line to hit the ~CLeakTrack assert to see what's looking like it's leaking
// #define LEAKTRACK

#ifdef LEAKTRACK

class CLeakTrack
{
public:
	CLeakTrack()
	{
	}
	~CLeakTrack()
	{
		if ( keys.Count() != 0 )
		{
			Assert( 0 );
		}
	}

	struct kve
	{
		KeyValues *kv;
		char		name[ 256 ];
	};

	void AddKv( KeyValues *kv, char const *name )
	{
		kve k;
		strncpy( k.name, name ? name : "NULL", sizeof( k.name ) );
		k.kv = kv;

		keys.AddToTail( k );
	}

	void RemoveKv( KeyValues *kv )
	{
		int c = keys.Count();
		for ( int i = 0; i < c; i++ )
		{
			if ( keys[i].kv == kv )
			{
				keys.Remove( i );
				break;
			}
		}
	}

	CUtlVector< kve > keys;
};

static CLeakTrack track;

#define TRACK_KV_ADD( ptr, name )	track.AddKv( ptr, name )
#define TRACK_KV_REMOVE( ptr )		track.RemoveKv( ptr )

#else

#define TRACK_KV_ADD( ptr, name ) 
#define TRACK_KV_REMOVE( ptr )	

#endif


//-----------------------------------------------------------------------------
// Purpose: Sets whether the KeyValues system should use an arbitrarily growable
//	string table. See the comment in the header for more info.
//-----------------------------------------------------------------------------
void KeyValues::SetUseGrowableStringTable( bool bUseGrowableTable )
{
	Assert( false );
}


//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
KeyValues::KeyValues( const char *setName )
{
	TRACK_KV_ADD( this, setName );

	Init();
	SetName ( setName );
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
KeyValues::KeyValues( const char *setName, const char *firstKey, const char *firstValue )
{
	TRACK_KV_ADD( this, setName );

	Init();
	SetName( setName );
	SetString( firstKey, firstValue );
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
KeyValues::KeyValues( const char *setName, const char *firstKey, const wchar_t *firstValue )
{
	TRACK_KV_ADD( this, setName );

	Init();
	SetName( setName );
	SetWString( firstKey, firstValue );
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
KeyValues::KeyValues( const char *setName, const char *firstKey, int firstValue )
{
	TRACK_KV_ADD( this, setName );

	Init();
	SetName( setName );
	SetInt( firstKey, firstValue );
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
KeyValues::KeyValues( const char *setName, const char *firstKey, const char *firstValue, const char *secondKey, const char *secondValue )
{
	TRACK_KV_ADD( this, setName );

	Init();
	SetName( setName );
	SetString( firstKey, firstValue );
	SetString( secondKey, secondValue );
}

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
KeyValues::KeyValues( const char *setName, const char *firstKey, int firstValue, const char *secondKey, int secondValue )
{
	TRACK_KV_ADD( this, setName );

	Init();
	SetName( setName );
	SetInt( firstKey, firstValue );
	SetInt( secondKey, secondValue );
}

//-----------------------------------------------------------------------------
// Purpose: Initialize member variables
//-----------------------------------------------------------------------------
void KeyValues::Init()
{
	m_iKeyName = NULL;
	m_iDataType = TYPE_NONE;

	m_pSub = NULL;
	m_pPeer = NULL;
	m_pChain = NULL;

	m_sValue = NULL;
	m_wsValue = NULL;
	m_pValue = NULL;
	
	m_bHasEscapeSequences = false;
	m_bEvaluateConditionals = true;

	// for future proof
	memset( unused, 0, sizeof(unused) );
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
KeyValues::~KeyValues()
{
	TRACK_KV_REMOVE( this );

	RemoveEverything();
}

//-----------------------------------------------------------------------------
// Purpose: remove everything
//-----------------------------------------------------------------------------
void KeyValues::RemoveEverything()
{
	KeyValues *dat;
	KeyValues *datNext = NULL;
	for ( dat = m_pSub; dat != NULL; dat = datNext )
	{
		datNext = dat->m_pPeer;
		dat->m_pPeer = NULL;
		delete dat;
	}

	for ( dat = m_pPeer; dat && dat != this; dat = datNext )
	{
		datNext = dat->m_pPeer;
		dat->m_pPeer = NULL;
		delete dat;
	}

	delete [] m_sValue;
	m_sValue = NULL;
	delete [] m_wsValue;
	m_wsValue = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *f - 
//-----------------------------------------------------------------------------

void KeyValues::RecursiveSaveToFile( CUtlBuffer& buf, int indentLevel, bool sortKeys /*= false*/, bool bAllowEmptyString /*= false*/ )
{
	
}

//-----------------------------------------------------------------------------
// Adds a chain... if we don't find stuff in this keyvalue, we'll look
// in the one we're chained to.
//-----------------------------------------------------------------------------

void KeyValues::ChainKeyValue( KeyValues* pChain )
{
	m_pChain = pChain;
}

//-----------------------------------------------------------------------------
// Purpose: Get the name of the current key section
//-----------------------------------------------------------------------------
const char *KeyValues::GetName( void ) const
{
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Read a single token from buffer (0 terminated)
//-----------------------------------------------------------------------------
#pragma warning (disable:4706)
const char *KeyValues::ReadToken( CUtlBuffer &buf, bool &wasQuoted, bool &wasConditional )
{
	wasQuoted = false;
	wasConditional = false;

	if ( !buf.IsValid() )
		return NULL; 

	// eating white spaces and remarks loop
	while ( true )
	{
		buf.EatWhiteSpace();
		if ( !buf.IsValid() )
			return NULL;	// file ends after reading whitespaces

		// stop if it's not a comment; a new token starts here
		if ( !buf.EatCPPComment() )
			break;
	}

	const char *c = (const char*)buf.PeekGet( sizeof(char), 0 );
	if ( !c )
		return NULL;

	// read quoted strings specially
	if ( *c == '\"' )
	{
		wasQuoted = true;
		buf.GetDelimitedString( m_bHasEscapeSequences ? GetCStringCharConversion() : GetNoEscCharConversion(), 
			s_pTokenBuf, KEYVALUES_TOKEN_SIZE );
		return s_pTokenBuf;
	}

	if ( *c == '{' || *c == '}' )
	{
		// it's a control char, just add this one char and stop reading
		s_pTokenBuf[0] = *c;
		s_pTokenBuf[1] = 0;
		buf.SeekGet( CUtlBuffer::SEEK_CURRENT, 1 );
		return s_pTokenBuf;
	}

	// read in the token until we hit a whitespace or a control character
	bool bReportedError = false;
	bool bConditionalStart = false;
	int nCount = 0;
	while ( ( c = (const char*)buf.PeekGet( sizeof(char), 0 ) ) )
	{
		// end of file
		if ( *c == 0 )
			break;

		// break if any control character appears in non quoted tokens
		if ( *c == '"' || *c == '{' || *c == '}' )
			break;

		if ( *c == '[' )
			bConditionalStart = true;

		if ( *c == ']' && bConditionalStart )
		{
			wasConditional = true;
		}

		// break on whitespace
		if ( isspace(*c) )
			break;

		if (nCount < (KEYVALUES_TOKEN_SIZE-1) )
		{
			s_pTokenBuf[nCount++] = *c;	// add char to buffer
		}
		else if ( !bReportedError )
		{
			bReportedError = true;
			g_KeyValuesErrorStack.ReportError(" ReadToken overflow" );
		}

		buf.SeekGet( CUtlBuffer::SEEK_CURRENT, 1 );
	}
	s_pTokenBuf[ nCount ] = 0;
	return s_pTokenBuf;
}
#pragma warning (default:4706)

	

//-----------------------------------------------------------------------------
// Purpose: if parser should translate escape sequences ( /n, /t etc), set to true
//-----------------------------------------------------------------------------
void KeyValues::UsesEscapeSequences(bool state)
{
	m_bHasEscapeSequences = state;
}


//-----------------------------------------------------------------------------
// Purpose: if parser should evaluate conditional blocks ( [$WINDOWS] etc. )
//-----------------------------------------------------------------------------
void KeyValues::UsesConditionals(bool state)
{
	m_bEvaluateConditionals = state;
}


//-----------------------------------------------------------------------------
// Purpose: Load keyValues from disk
//-----------------------------------------------------------------------------
bool KeyValues::LoadFromFile( IBaseFileSystem *filesystem, const char *resourceName, const char *pathID, bool refreshCache )
{
	Assert(false);

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Save the keyvalues to disk
//			Creates the path to the file if it doesn't exist
//-----------------------------------------------------------------------------
bool KeyValues::SaveToFile( IBaseFileSystem *filesystem, const char *resourceName, const char *pathID, bool sortKeys /*= false*/, bool bAllowEmptyString /*= false*/, bool bCacheResult /*= false*/ )
{
	Assert( false );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Write out a set of indenting
//-----------------------------------------------------------------------------
void KeyValues::WriteIndents( IBaseFileSystem *filesystem, FileHandle_t f, CUtlBuffer *pBuf, int indentLevel )
{
	for ( int i = 0; i < indentLevel; i++ )
	{
		INTERNALWRITE( "\t", 1 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Write out a string where we convert the double quotes to backslash double quote
//-----------------------------------------------------------------------------
void KeyValues::WriteConvertedString( IBaseFileSystem *filesystem, FileHandle_t f, CUtlBuffer *pBuf, const char *pszString )
{
	// handle double quote chars within the string
	// the worst possible case is that the whole string is quotes
	int len = strlen(pszString);
	char *convertedString = (char *) stackalloc ((len + 1)  * sizeof(char) * 2);
	int j=0;
	for (int i=0; i <= len; i++)
	{
		if (pszString[i] == '\"')
		{
			convertedString[j] = '\\';
			j++;
		}
		else if ( m_bHasEscapeSequences && pszString[i] == '\\' )
		{
			convertedString[j] = '\\';
			j++;
		}
		convertedString[j] = pszString[i];
		j++;
	}		

	INTERNALWRITE(convertedString, strlen(convertedString));
}


void KeyValues::InternalWrite( IBaseFileSystem *filesystem, FileHandle_t f, CUtlBuffer *pBuf, const void *pData, int len )
{
	Assert( false );
}

//-----------------------------------------------------------------------------
// Purpose: Save keyvalues from disk, if subkey values are detected, calls
//			itself to save those
//-----------------------------------------------------------------------------
void KeyValues::RecursiveSaveToFile( IBaseFileSystem *filesystem, FileHandle_t f, CUtlBuffer *pBuf, int indentLevel, bool sortKeys, bool bAllowEmptyString )
{
	Assert( false );
}

void KeyValues::SaveKeyToFile( KeyValues *dat, IBaseFileSystem *filesystem, FileHandle_t f, CUtlBuffer *pBuf, int indentLevel, bool sortKeys, bool bAllowEmptyString )
{
	if ( dat->m_pSub )
	{
		dat->RecursiveSaveToFile( filesystem, f, pBuf, indentLevel + 1, sortKeys, bAllowEmptyString );
	}
	else
	{
		// only write non-empty keys

		switch (dat->m_iDataType)
		{
		case TYPE_STRING:
			{
				if ( dat->m_sValue && ( bAllowEmptyString || *(dat->m_sValue) ) )
				{
					WriteIndents(filesystem, f, pBuf, indentLevel + 1);
					INTERNALWRITE("\"", 1);
					WriteConvertedString(filesystem, f, pBuf, dat->GetName());	
					INTERNALWRITE("\"\t\t\"", 4);

					WriteConvertedString(filesystem, f, pBuf, dat->m_sValue);	

					INTERNALWRITE("\"\n", 2);
				}
				break;
			}
		case TYPE_WSTRING:
			{
				Assert( false );
				break;
			}

		case TYPE_INT:
			{
				WriteIndents(filesystem, f, pBuf, indentLevel + 1);
				INTERNALWRITE("\"", 1);
				INTERNALWRITE(dat->GetName(), strlen(dat->GetName()));
				INTERNALWRITE("\"\t\t\"", 4);

				char buf[32];
				snprintf(buf, sizeof( buf ), "%d", dat->m_iValue);

				INTERNALWRITE(buf, strlen(buf));
				INTERNALWRITE("\"\n", 2);
				break;
			}

		case TYPE_UINT64:
			{
				WriteIndents(filesystem, f, pBuf, indentLevel + 1);
				INTERNALWRITE("\"", 1);
				INTERNALWRITE(dat->GetName(), strlen(dat->GetName()));
				INTERNALWRITE("\"\t\t\"", 4);

				char buf[32];
				// write "0x" + 16 char 0-padded hex encoded 64 bit value
#ifdef WIN32
				snprintf( buf, sizeof( buf ), "0x%016I64X", *( (uint64_t *)dat->m_sValue ) );
#else
				snprintf( buf, sizeof( buf ), "0x%016llX", *( (uint64_t *)dat->m_sValue ) );
#endif

				INTERNALWRITE(buf, strlen(buf));
				INTERNALWRITE("\"\n", 2);
				break;
			}

		case TYPE_FLOAT:
			{
				WriteIndents(filesystem, f, pBuf, indentLevel + 1);
				INTERNALWRITE("\"", 1);
				INTERNALWRITE(dat->GetName(), strlen(dat->GetName()));
				INTERNALWRITE("\"\t\t\"", 4);

				char buf[48];
				snprintf(buf, sizeof( buf ), "%f", dat->m_flValue);

				INTERNALWRITE(buf, strlen(buf));
				INTERNALWRITE("\"\n", 2);
				break;
			}
		case TYPE_COLOR:
			//DevMsg(1, "KeyValues::RecursiveSaveToFile: TODO, missing code for TYPE_COLOR.\n");
			break;

		default:
			break;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: looks up a key by symbol name
//-----------------------------------------------------------------------------
KeyValues *KeyValues::FindKey(int keySymbol) const
{
	for (KeyValues *dat = m_pSub; dat != NULL; dat = dat->m_pPeer)
	{
		if (dat->m_iKeyName == keySymbol)
			return dat;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Find a keyValue, create it if it is not found.
//			Set bCreate to true to create the key if it doesn't already exist 
//			(which ensures a valid pointer will be returned)
//-----------------------------------------------------------------------------
KeyValues *KeyValues::FindKey(const char *keyName, bool bCreate)
{
	Assert( false );
	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Create a new key, with an autogenerated name.  
//			Name is guaranteed to be an integer, of value 1 higher than the highest 
//			other integer key name
//-----------------------------------------------------------------------------
KeyValues *KeyValues::CreateNewKey()
{
	int newID = 1;

	// search for any key with higher values
	KeyValues *pLastChild = NULL;
	for (KeyValues *dat = m_pSub; dat != NULL; dat = dat->m_pPeer)
	{
		// case-insensitive string compare
		int val = atoi(dat->GetName());
		if (newID <= val)
		{
			newID = val + 1;
		}

		pLastChild = dat;
	}

	char buf[12];
	snprintf( buf, sizeof(buf), "%d", newID );

	return CreateKeyUsingKnownLastChild( buf, pLastChild );
}


//-----------------------------------------------------------------------------
// Create a key
//-----------------------------------------------------------------------------
KeyValues* KeyValues::CreateKey( const char *keyName )
{
	KeyValues *pLastChild = FindLastSubKey();
	return CreateKeyUsingKnownLastChild( keyName, pLastChild );
}

//-----------------------------------------------------------------------------
KeyValues* KeyValues::CreateKeyUsingKnownLastChild( const char *keyName, KeyValues *pLastChild )
{
	// Create a new key
	KeyValues* dat = new KeyValues( keyName );

	dat->UsesEscapeSequences( m_bHasEscapeSequences != 0 ); // use same format as parent does
	dat->UsesConditionals( m_bEvaluateConditionals != 0 );
	
	// add into subkey list
	AddSubkeyUsingKnownLastChild( dat, pLastChild );

	return dat;
}

//-----------------------------------------------------------------------------
void KeyValues::AddSubkeyUsingKnownLastChild( KeyValues *pSubkey, KeyValues *pLastChild )
{
	// Make sure the subkey isn't a child of some other keyvalues
	Assert( pSubkey != NULL );
	Assert( pSubkey->m_pPeer == NULL );

	// Empty child list?
	if ( pLastChild == NULL )
	{
		Assert( m_pSub == NULL );
		m_pSub = pSubkey;
	}
	else
	{
		Assert( m_pSub != NULL );
		Assert( pLastChild->m_pPeer == NULL );

//		// In debug, make sure that they really do know which child is the last one
//		#ifdef _DEBUG
//			KeyValues *pTempDat = m_pSub;
//			while ( pTempDat->GetNextKey() != NULL )
//			{
//				pTempDat = pTempDat->GetNextKey();
//			}
//			Assert( pTempDat == pLastChild );
//		#endif

		pLastChild->SetNextKey( pSubkey );
	}
}


//-----------------------------------------------------------------------------
// Adds a subkey. Make sure the subkey isn't a child of some other keyvalues
//-----------------------------------------------------------------------------
void KeyValues::AddSubKey( KeyValues *pSubkey )
{
	// Make sure the subkey isn't a child of some other keyvalues
	Assert( pSubkey != NULL );
	Assert( pSubkey->m_pPeer == NULL );

	// add into subkey list
	if ( m_pSub == NULL )
	{
		m_pSub = pSubkey;
	}
	else
	{
		KeyValues *pTempDat = m_pSub;
		while ( pTempDat->GetNextKey() != NULL )
		{
			pTempDat = pTempDat->GetNextKey();
		}

		pTempDat->SetNextKey( pSubkey );
	}
}


	
//-----------------------------------------------------------------------------
// Purpose: Remove a subkey from the list
//-----------------------------------------------------------------------------
void KeyValues::RemoveSubKey(KeyValues *subKey)
{
	if (!subKey)
		return;

	// check the list pointer
	if (m_pSub == subKey)
	{
		m_pSub = subKey->m_pPeer;
	}
	else
	{
		// look through the list
		KeyValues *kv = m_pSub;
		while (kv->m_pPeer)
		{
			if (kv->m_pPeer == subKey)
			{
				kv->m_pPeer = subKey->m_pPeer;
				break;
			}
			
			kv = kv->m_pPeer;
		}
	}

	subKey->m_pPeer = NULL;
}



//-----------------------------------------------------------------------------
// Purpose: Locate last child.  Returns NULL if we have no children
//-----------------------------------------------------------------------------
KeyValues *KeyValues::FindLastSubKey()
{

	// No children?
	if ( m_pSub == NULL )
		return NULL;

	// Scan for the last one
	KeyValues *pLastChild = m_pSub;
	while ( pLastChild->m_pPeer )
		pLastChild = pLastChild->m_pPeer;
	return pLastChild;
}

//-----------------------------------------------------------------------------
// Purpose: Sets this key's peer to the KeyValues passed in
//-----------------------------------------------------------------------------
void KeyValues::SetNextKey( KeyValues *pDat )
{
	m_pPeer = pDat;
}


KeyValues* KeyValues::GetFirstTrueSubKey()
{
	KeyValues *pRet = m_pSub;
	while ( pRet && pRet->m_iDataType != TYPE_NONE )
		pRet = pRet->m_pPeer;

	return pRet;
}

KeyValues* KeyValues::GetNextTrueSubKey()
{
	KeyValues *pRet = m_pPeer;
	while ( pRet && pRet->m_iDataType != TYPE_NONE )
		pRet = pRet->m_pPeer;

	return pRet;
}

KeyValues* KeyValues::GetFirstValue()
{
	KeyValues *pRet = m_pSub;
	while ( pRet && pRet->m_iDataType == TYPE_NONE )
		pRet = pRet->m_pPeer;

	return pRet;
}

KeyValues* KeyValues::GetNextValue()
{
	KeyValues *pRet = m_pPeer;
	while ( pRet && pRet->m_iDataType == TYPE_NONE )
		pRet = pRet->m_pPeer;

	return pRet;
}


//-----------------------------------------------------------------------------
// Purpose: Get the integer value of a keyName. Default value is returned
//			if the keyName can't be found.
//-----------------------------------------------------------------------------
int KeyValues::GetInt( const char *keyName, int defaultValue )
{
	KeyValues *dat = FindKey( keyName, false );
	if ( dat )
	{
		switch ( dat->m_iDataType )
		{
		case TYPE_STRING:
			return atoi(dat->m_sValue);
		case TYPE_WSTRING:
			return _wtoi(dat->m_wsValue);
		case TYPE_FLOAT:
			return (int)dat->m_flValue;
		case TYPE_UINT64:
			// can't convert, since it would lose data
			Assert(0);
			return 0;
		case TYPE_INT:
		case TYPE_PTR:
		default:
			return dat->m_iValue;
		};
	}
	return defaultValue;
}

//-----------------------------------------------------------------------------
// Purpose: Get the integer value of a keyName. Default value is returned
//			if the keyName can't be found.
//-----------------------------------------------------------------------------
uint64_t KeyValues::GetUint64( const char *keyName, uint64_t defaultValue )
{
	KeyValues *dat = FindKey( keyName, false );
	if ( dat )
	{
		switch ( dat->m_iDataType )
		{
		case TYPE_STRING:
			return (uint64_t)_atoi64(dat->m_sValue);
		case TYPE_WSTRING:
			return _wtoi64(dat->m_wsValue);
		case TYPE_FLOAT:
			return (int)dat->m_flValue;
		case TYPE_UINT64:
			return *((uint64_t*)dat->m_sValue);
		case TYPE_PTR:
			return (uint64_t)(uintptr_t)dat->m_pValue;
		case TYPE_INT:
		default:
			return dat->m_iValue;
		};
	}
	return defaultValue;
}

//-----------------------------------------------------------------------------
// Purpose: Get the pointer value of a keyName. Default value is returned
//			if the keyName can't be found.
//-----------------------------------------------------------------------------
void *KeyValues::GetPtr( const char *keyName, void *defaultValue )
{
	KeyValues *dat = FindKey( keyName, false );
	if ( dat )
	{
		switch ( dat->m_iDataType )
		{
		case TYPE_PTR:
			return dat->m_pValue;

		case TYPE_WSTRING:
		case TYPE_STRING:
		case TYPE_FLOAT:
		case TYPE_INT:
		case TYPE_UINT64:
		default:
			return NULL;
		};
	}
	return defaultValue;
}

//-----------------------------------------------------------------------------
// Purpose: Get the float value of a keyName. Default value is returned
//			if the keyName can't be found.
//-----------------------------------------------------------------------------
float KeyValues::GetFloat( const char *keyName, float defaultValue )
{
	KeyValues *dat = FindKey( keyName, false );
	if ( dat )
	{
		switch ( dat->m_iDataType )
		{
		case TYPE_STRING:
			return (float)atof(dat->m_sValue);
		case TYPE_WSTRING:
#ifdef WIN32
			return (float) _wtof(dat->m_wsValue);		// no wtof
#else
			Assert( !"impl me" );
			return 0.0;
#endif
			case TYPE_FLOAT:
			return dat->m_flValue;
		case TYPE_INT:
			return (float)dat->m_iValue;
		case TYPE_UINT64:
			return (float)(*((uint64_t*)dat->m_sValue));
		case TYPE_PTR:
		default:
			return 0.0f;
		};
	}
	return defaultValue;
}

//-----------------------------------------------------------------------------
// Purpose: Get the string pointer of a keyName. Default value is returned
//			if the keyName can't be found.
//-----------------------------------------------------------------------------
const char *KeyValues::GetString( const char *keyName, const char *defaultValue )
{
	KeyValues *dat = FindKey( keyName, false );
	if ( dat )
	{
		// convert the data to string form then return it
		char buf[64];
		switch ( dat->m_iDataType )
		{
		case TYPE_FLOAT:
			snprintf( buf, sizeof( buf ), "%f", dat->m_flValue );
			SetString( keyName, buf );
			break;
		case TYPE_PTR:
			snprintf( buf, sizeof( buf ), "%lld", (int64_t)(size_t)dat->m_pValue );
			SetString( keyName, buf );
			break;
		case TYPE_INT:
			snprintf( buf, sizeof( buf ), "%d", dat->m_iValue );
			SetString( keyName, buf );
			break;
		case TYPE_UINT64:
			snprintf( buf, sizeof( buf ), "%lld", *((uint64_t*)(dat->m_sValue)) );
			SetString( keyName, buf );
			break;

		case TYPE_WSTRING:
		{
			Assert( false );
			break;
		}
		case TYPE_STRING:
			break;
		default:
			return defaultValue;
		};
		
		return dat->m_sValue;
	}
	return defaultValue;
}


const wchar_t *KeyValues::GetWString( const char *keyName, const wchar_t *defaultValue)
{
	KeyValues *dat = FindKey( keyName, false );
	if ( dat )
	{
		wchar_t wbuf[64];
		switch ( dat->m_iDataType )
		{
		case TYPE_FLOAT:
			swprintf(wbuf, ARRAYSIZE(wbuf), L"%f", dat->m_flValue);
			SetWString( keyName, wbuf);
			break;
		case TYPE_PTR:
			swprintf( wbuf, ARRAYSIZE(wbuf), L"%lld", (int64_t)(size_t)dat->m_pValue );
			SetWString( keyName, wbuf );
			break;
		case TYPE_INT:
			swprintf( wbuf, ARRAYSIZE(wbuf), L"%d", dat->m_iValue );
			SetWString( keyName, wbuf );
			break;
		case TYPE_UINT64:
			{
				swprintf( wbuf, ARRAYSIZE(wbuf), L"%lld", *((uint64_t *)(dat->m_sValue)) );
				SetWString( keyName, wbuf );
			}
			break;

		case TYPE_WSTRING:
			break;
		case TYPE_STRING:
		{
			Assert( false );
			break;
		}
		default:
			return defaultValue;
		};
		
		return (const wchar_t* )dat->m_wsValue;
	}
	return defaultValue;
}

//-----------------------------------------------------------------------------
// Purpose: Get a bool interpretation of the key.
//-----------------------------------------------------------------------------
bool KeyValues::GetBool( const char *keyName, bool defaultValue, bool* optGotDefault )
{
	if ( FindKey( keyName ) )
    {
        if ( optGotDefault )
		{
            *optGotDefault = false;
		}

		return 0 != GetInt( keyName, 0 );
    }
    
    if ( optGotDefault )
	{
        *optGotDefault = true;
	}

	return defaultValue;
}

//-----------------------------------------------------------------------------
// Purpose: Gets a color
//-----------------------------------------------------------------------------
Color KeyValues::GetColor( const char *keyName )
{
	Color color(0, 0, 0, 0);
	KeyValues *dat = FindKey( keyName, false );
	if ( dat )
	{
		if ( dat->m_iDataType == TYPE_COLOR )
		{
			color[0] = dat->m_Color[0];
			color[1] = dat->m_Color[1];
			color[2] = dat->m_Color[2];
			color[3] = dat->m_Color[3];
		}
		else if ( dat->m_iDataType == TYPE_FLOAT )
		{
			color[0] = dat->m_flValue;
		}
		else if ( dat->m_iDataType == TYPE_INT )
		{
			color[0] = dat->m_iValue;
		}
		else if ( dat->m_iDataType == TYPE_STRING )
		{
			// parse the colors out of the string
			float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
			sscanf(dat->m_sValue, "%f %f %f %f", &a, &b, &c, &d);
			color[0] = (unsigned char)a;
			color[1] = (unsigned char)b;
			color[2] = (unsigned char)c;
			color[3] = (unsigned char)d;
		}
	}
	return color;
}

//-----------------------------------------------------------------------------
// Purpose: Sets a color
//-----------------------------------------------------------------------------
void KeyValues::SetColor( const char *keyName, Color value)
{
	KeyValues *dat = FindKey( keyName, true );

	if ( dat )
	{
		dat->m_iDataType = TYPE_COLOR;
		dat->m_Color[0] = value[0];
		dat->m_Color[1] = value[1];
		dat->m_Color[2] = value[2];
		dat->m_Color[3] = value[3];
	}
}

void KeyValues::SetStringValue( char const *strValue )
{
	// delete the old value
	delete [] m_sValue;
	// make sure we're not storing the WSTRING  - as we're converting over to STRING
	delete [] m_wsValue;
	m_wsValue = NULL;

	if (!strValue)
	{
		// ensure a valid value
		strValue = "";
	}

	// allocate memory for the new value and copy it in
	int len = strlen( strValue );
	m_sValue = new char[len + 1];
	memcpy( m_sValue, strValue, len+1 );

	m_iDataType = TYPE_STRING;
}

//-----------------------------------------------------------------------------
// Purpose: Set the string value of a keyName. 
//-----------------------------------------------------------------------------
void KeyValues::SetString( const char *keyName, const char *value )
{
	KeyValues *dat = FindKey( keyName, true );

	if ( dat )
	{
		if ( dat->m_iDataType == TYPE_STRING && dat->m_sValue == value )
		{
			return;
		}

		// delete the old value
		delete [] dat->m_sValue;
		// make sure we're not storing the WSTRING  - as we're converting over to STRING
		delete [] dat->m_wsValue;
		dat->m_wsValue = NULL;

		if (!value)
		{
			// ensure a valid value
			value = "";
		}

		// allocate memory for the new value and copy it in
		int len = strlen( value );
		dat->m_sValue = new char[len + 1];
		memcpy( dat->m_sValue, value, len+1 );

		dat->m_iDataType = TYPE_STRING;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Set the string value of a keyName. 
//-----------------------------------------------------------------------------
void KeyValues::SetWString( const char *keyName, const wchar_t *value )
{
	KeyValues *dat = FindKey( keyName, true );
	if ( dat )
	{
		// delete the old value
		delete [] dat->m_wsValue;
		// make sure we're not storing the STRING  - as we're converting over to WSTRING
		delete [] dat->m_sValue;
		dat->m_sValue = NULL;

		if (!value)
		{
			// ensure a valid value
			value = L"";
		}

		// allocate memory for the new value and copy it in
		int len = wcslen( value );
		dat->m_wsValue = new wchar_t[len + 1];
		memcpy( dat->m_wsValue, value, (len+1) * sizeof(wchar_t) );

		dat->m_iDataType = TYPE_WSTRING;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Set the integer value of a keyName. 
//-----------------------------------------------------------------------------
void KeyValues::SetInt( const char *keyName, int value )
{
	KeyValues *dat = FindKey( keyName, true );

	if ( dat )
	{
		dat->m_iValue = value;
		dat->m_iDataType = TYPE_INT;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Set the integer value of a keyName. 
//-----------------------------------------------------------------------------
void KeyValues::SetUint64( const char *keyName, uint64_t value )
{
	KeyValues *dat = FindKey( keyName, true );

	if ( dat )
	{
		// delete the old value
		delete [] dat->m_sValue;
		// make sure we're not storing the WSTRING  - as we're converting over to STRING
		delete [] dat->m_wsValue;
		dat->m_wsValue = NULL;

		dat->m_sValue = new char[sizeof(uint64_t)];
		*((uint64_t *)dat->m_sValue) = value;
		dat->m_iDataType = TYPE_UINT64;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Set the float value of a keyName. 
//-----------------------------------------------------------------------------
void KeyValues::SetFloat( const char *keyName, float value )
{
	KeyValues *dat = FindKey( keyName, true );

	if ( dat )
	{
		dat->m_flValue = value;
		dat->m_iDataType = TYPE_FLOAT;
	}
}

void KeyValues::SetName( const char * setName )
{
	m_iKeyName = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Set the pointer value of a keyName. 
//-----------------------------------------------------------------------------
void KeyValues::SetPtr( const char *keyName, void *value )
{
	KeyValues *dat = FindKey( keyName, true );

	if ( dat )
	{
		dat->m_pValue = value;
		dat->m_iDataType = TYPE_PTR;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Copies the tree from the other KeyValues into this one, recursively
// beginning with the root specified by rootSrc.
//-----------------------------------------------------------------------------
void KeyValues::CopyKeyValuesFromRecursive( const KeyValues& rootSrc )
{
	Assert( false );
}

//-----------------------------------------------------------------------------
// Purpose: Copies a single KeyValue from src to this, using the provided temporary
// buffer if the keytype requires it. Does NOT recurse.
//-----------------------------------------------------------------------------
void KeyValues::CopyKeyValue( const KeyValues& src, size_t tmpBufferSizeB, char* tmpBuffer )
{
	m_iKeyName = src.GetNameSymbol();

	if ( src.m_pSub )
		return;

	m_iDataType = src.m_iDataType;
		
	switch( src.m_iDataType )
	{
	case TYPE_NONE:
		break;
	case TYPE_STRING:
		if( src.m_sValue )
		{
			int len = strlen(src.m_sValue) + 1;
			m_sValue = new char[len];
			strncpy( m_sValue, src.m_sValue, len );
		}
		break;
	case TYPE_INT:
		{
			m_iValue = src.m_iValue;
			snprintf( tmpBuffer, (int)tmpBufferSizeB, "%d", m_iValue );
			int len = strlen(tmpBuffer) + 1;
			m_sValue = new char[len];
			strncpy( m_sValue, tmpBuffer, len  );
		}
		break;
	case TYPE_FLOAT:
		{
			m_flValue = src.m_flValue;
			snprintf( tmpBuffer, (int)tmpBufferSizeB, "%f", m_flValue );
			int len = strlen(tmpBuffer) + 1;
			m_sValue = new char[len];
			strncpy( m_sValue, tmpBuffer, len );
		}
		break;
	case TYPE_PTR:
		{
			m_pValue = src.m_pValue;
		}
		break;
	case TYPE_UINT64:
		{
			m_sValue = new char[sizeof(uint64_t)];
			memcpy( m_sValue, src.m_sValue, sizeof(uint64_t) );
		}
		break;
	case TYPE_COLOR:
		{
			m_Color[0] = src.m_Color[0];
			m_Color[1] = src.m_Color[1];
			m_Color[2] = src.m_Color[2];
			m_Color[3] = src.m_Color[3];
		}
		break;
			
	default:
		{
			// do nothing . .what the heck is this?
			Assert( 0 );
		}
		break;
	}
}

KeyValues& KeyValues::operator=( const KeyValues& src )
{
	RemoveEverything();
	Init();	// reset all values
	CopyKeyValuesFromRecursive( src );
	return *this;
}


//-----------------------------------------------------------------------------
// Make a new copy of all subkeys, add them all to the passed-in keyvalues
//-----------------------------------------------------------------------------
void KeyValues::CopySubkeys( KeyValues *pParent ) const
{
	// recursively copy subkeys
	// Also maintain ordering....
	KeyValues *pPrev = NULL;
	for ( KeyValues *sub = m_pSub; sub != NULL; sub = sub->m_pPeer )
	{
		// take a copy of the subkey
		KeyValues *dat = sub->MakeCopy();
		 
		// add into subkey list
		if (pPrev)
		{
			pPrev->m_pPeer = dat;
		}
		else
		{
			pParent->m_pSub = dat;
		}
		dat->m_pPeer = NULL;
		pPrev = dat;
	}
}


//-----------------------------------------------------------------------------
// Purpose: Makes a copy of the whole key-value pair set
//-----------------------------------------------------------------------------
KeyValues *KeyValues::MakeCopy( void ) const
{
	KeyValues *newKeyValue = new KeyValues(GetName());

	newKeyValue->UsesEscapeSequences( m_bHasEscapeSequences != 0 );
	newKeyValue->UsesConditionals( m_bEvaluateConditionals != 0 );

	// copy data
	newKeyValue->m_iDataType = m_iDataType;
	switch ( m_iDataType )
	{
	case TYPE_STRING:
		{
			if ( m_sValue )
			{
				int len = strlen( m_sValue );
				Assert( !newKeyValue->m_sValue );
				newKeyValue->m_sValue = new char[len + 1];
				memcpy( newKeyValue->m_sValue, m_sValue, len+1 );
			}
		}
		break;
	case TYPE_WSTRING:
		{
			if ( m_wsValue )
			{
				int len = wcslen( m_wsValue );
				newKeyValue->m_wsValue = new wchar_t[len+1];
				memcpy( newKeyValue->m_wsValue, m_wsValue, (len+1)*sizeof(wchar_t));
			}
		}
		break;

	case TYPE_INT:
		newKeyValue->m_iValue = m_iValue;
		break;

	case TYPE_FLOAT:
		newKeyValue->m_flValue = m_flValue;
		break;

	case TYPE_PTR:
		newKeyValue->m_pValue = m_pValue;
		break;
		
	case TYPE_COLOR:
		newKeyValue->m_Color[0] = m_Color[0];
		newKeyValue->m_Color[1] = m_Color[1];
		newKeyValue->m_Color[2] = m_Color[2];
		newKeyValue->m_Color[3] = m_Color[3];
		break;

	case TYPE_UINT64:
		newKeyValue->m_sValue = new char[sizeof(uint64_t)];
		memcpy( newKeyValue->m_sValue, m_sValue, sizeof(uint64_t) );
		break;
	};

	// recursively copy subkeys
	CopySubkeys( newKeyValue );
	return newKeyValue;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
KeyValues *KeyValues::MakeCopy( bool copySiblings ) const
{
	KeyValues* rootDest = MakeCopy();
	if ( !copySiblings )
		return rootDest;

	const KeyValues* curSrc = GetNextKey();
	KeyValues* curDest = rootDest;
	while (curSrc) {
		curDest->SetNextKey( curSrc->MakeCopy() );
		curDest = curDest->GetNextKey();
		curSrc = curSrc->GetNextKey();
	}

	return rootDest;
}

//-----------------------------------------------------------------------------
// Purpose: Check if a keyName has no value assigned to it.
//-----------------------------------------------------------------------------
bool KeyValues::IsEmpty(const char *keyName)
{
	KeyValues *dat = FindKey(keyName, false);
	if (!dat)
		return true;

	if (dat->m_iDataType == TYPE_NONE && dat->m_pSub == NULL)
		return true;

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Clear out all subkeys, and the current value
//-----------------------------------------------------------------------------
void KeyValues::Clear( void )
{
	delete m_pSub;
	m_pSub = NULL;
	m_iDataType = TYPE_NONE;
}

//-----------------------------------------------------------------------------
// Purpose: Get the data type of the value stored in a keyName
//-----------------------------------------------------------------------------
KeyValues::types_t KeyValues::GetDataType(const char *keyName)
{
	KeyValues *dat = FindKey(keyName, false);
	if (dat)
		return (types_t)dat->m_iDataType;

	return TYPE_NONE;
}

//-----------------------------------------------------------------------------
// Purpose: Deletion, ensures object gets deleted from correct heap
//-----------------------------------------------------------------------------
void KeyValues::deleteThis()
{
	delete this;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : includedKeys - 
//-----------------------------------------------------------------------------
void KeyValues::AppendIncludedKeys( CUtlVector< KeyValues * >& includedKeys )
{
	// Append any included keys, too...
	KeyValues *insertSpot = this;
	int includeCount = includedKeys.Count();
	for ( int i = 0; i < includeCount; i++ )
	{
		KeyValues *kv = includedKeys[ i ];
		Assert( kv );

		while ( insertSpot->GetNextKey() )
		{
			insertSpot = insertSpot->GetNextKey();
		}

		insertSpot->SetNextKey( kv );
	}
}

void KeyValues::ParseIncludedKeys( char const *resourceName, const char *filetoinclude, 
		IBaseFileSystem* pFileSystem, const char *pPathID, CUtlVector< KeyValues * >& includedKeys )
{
	Assert( false );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : baseKeys - 
//-----------------------------------------------------------------------------
void KeyValues::MergeBaseKeys( CUtlVector< KeyValues * >& baseKeys )
{
	int includeCount = baseKeys.Count();
	int i;
	for ( i = 0; i < includeCount; i++ )
	{
		KeyValues *kv = baseKeys[ i ];
		Assert( kv );

		RecursiveMergeKeyValues( kv );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : baseKV - keyvalues we're basing ourselves on
//-----------------------------------------------------------------------------
void KeyValues::RecursiveMergeKeyValues( KeyValues *baseKV )
{
	// Merge ourselves
	// we always want to keep our value, so nothing to do here

	// Now merge our children
	for ( KeyValues *baseChild = baseKV->m_pSub; baseChild != NULL; baseChild = baseChild->m_pPeer )
	{
		// for each child in base, see if we have a matching kv

		bool bFoundMatch = false;

		// If we have a child by the same name, merge those keys
		for ( KeyValues *newChild = m_pSub; newChild != NULL; newChild = newChild->m_pPeer )
		{
			if ( !strcmp( baseChild->GetName(), newChild->GetName() ) )
			{
				newChild->RecursiveMergeKeyValues( baseChild );
				bFoundMatch = true;
				break;
			}	
		}

		// If not merged, append this key
		if ( !bFoundMatch )
		{
			KeyValues *dat = baseChild->MakeCopy();
			Assert( dat );
			AddSubKey( dat );
		}
	}
}

//-----------------------------------------------------------------------------
// Returns whether a keyvalues conditional evaluates to true or false
// Needs more flexibility with conditionals, checking convars would be nice.
//-----------------------------------------------------------------------------
bool EvaluateConditional( const char *str )
{
	if ( !str )
		return false;

	if ( *str == '[' )
		str++;

	bool bNot = false; // should we negate this command?
	if ( *str == '!' )
		bNot = true;
	
	if ( strstr( str, "$WIN32" ) )
		return true ^ bNot; // hack hack - for now WIN32 really means IsPC

	if ( strstr( str, "$WINDOWS" ) )
		return true ^ bNot;
	
	return false;
}

//-----------------------------------------------------------------------------
// Read from a buffer...
//-----------------------------------------------------------------------------
bool KeyValues::LoadFromBuffer( char const *resourceName, CUtlBuffer &buf, IBaseFileSystem* pFileSystem, const char *pPathID )
{
	Assert( false );
	return true;
}


//-----------------------------------------------------------------------------
// Read from a buffer...
//-----------------------------------------------------------------------------
bool KeyValues::LoadFromBuffer( char const *resourceName, const char *pBuffer, IBaseFileSystem* pFileSystem, const char *pPathID )
{
	Assert( false );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void KeyValues::RecursiveLoadFromBuffer( char const *resourceName, CUtlBuffer &buf )
{
	Assert( false );
}



// writes KeyValue as binary data to buffer
bool KeyValues::WriteAsBinary( CUtlBuffer &buffer )
{
	Assert( false );
	return buffer.IsValid();
}

// read KeyValues from binary buffer, returns true if parsing was successful
bool KeyValues::ReadAsBinary( CUtlBuffer &buffer, int nStackDepth )
{
	Assert( false );
	return buffer.IsValid();
}

bool IKeyValuesDumpContextAsText::KvBeginKey( KeyValues *pKey, int nIndentLevel )
{
	if ( pKey )
	{
		return
			KvWriteIndent( nIndentLevel ) &&
			KvWriteText( pKey->GetName() ) &&
			KvWriteText( "\n" ) &&
			KvWriteIndent( nIndentLevel ) &&
			KvWriteText( "{\n" );
	}
	else
	{
		return
			KvWriteIndent( nIndentLevel ) &&
			KvWriteText( "<< NULL >>\n" );
	}
}

bool IKeyValuesDumpContextAsText::KvWriteValue( KeyValues *val, int nIndentLevel )
{
	if ( !val )
	{
		return
			KvWriteIndent( nIndentLevel ) &&
			KvWriteText( "<< NULL >>\n" );
	}

	if ( !KvWriteIndent( nIndentLevel ) )
		return false;

	if ( !KvWriteText( val->GetName() ) )
		return false;

	if ( !KvWriteText( " " ) )
		return false;

	switch ( val->GetDataType() )
	{
	case KeyValues::TYPE_STRING:
		{
			if ( !KvWriteText( val->GetString() ) )
				return false;
		}
		break;

	case KeyValues::TYPE_INT:
		{
			int n = val->GetInt();
			char *chBuffer = ( char * ) stackalloc( 128 );
			V_snprintf( chBuffer, 128, "int( %d = 0x%X )", n, n );
			if ( !KvWriteText( chBuffer ) )
				return false;
		}
		break;
	
	case KeyValues::TYPE_FLOAT:
		{
			float fl = val->GetFloat();
			char *chBuffer = ( char * ) stackalloc( 128 );
			V_snprintf( chBuffer, 128, "float( %f )", fl );
			if ( !KvWriteText( chBuffer ) )
				return false;
		}
		break;

	case KeyValues::TYPE_PTR:
		{
			void *ptr = val->GetPtr();
			char *chBuffer = ( char * ) stackalloc( 128 );
			V_snprintf( chBuffer, 128, "ptr( 0x%p )", ptr );
			if ( !KvWriteText( chBuffer ) )
				return false;
		}
		break;

	case KeyValues::TYPE_WSTRING:
		{
			wchar_t const *wsz = val->GetWString();
			int nLen = wcslen( wsz );
			int numBytes = nLen*2 + 64;
			char *chBuffer = ( char * ) stackalloc( numBytes );
			V_snprintf( chBuffer, numBytes, "%ls [wstring, len = %d]", wsz, nLen );
			if ( !KvWriteText( chBuffer ) )
				return false;
		}
		break;

	case KeyValues::TYPE_UINT64:
		{
			uint64_t n = val->GetUint64();
			char *chBuffer = ( char * ) stackalloc( 128 );
			V_snprintf( chBuffer, 128, "u64( %lld = 0x%llX )", n, n );
			if ( !KvWriteText( chBuffer ) )
				return false;
		}
		break;

	default:
		break;
		{
			int n = val->GetDataType();
			char *chBuffer = ( char * ) stackalloc( 128 );
			V_snprintf( chBuffer, 128, "??kvtype[%d]", n );
			if ( !KvWriteText( chBuffer ) )
				return false;
		}
		break;
	}

	return KvWriteText( "\n" );
}

bool IKeyValuesDumpContextAsText::KvEndKey( KeyValues *pKey, int nIndentLevel )
{
	if ( pKey )
	{
		return
			KvWriteIndent( nIndentLevel ) &&
			KvWriteText( "}\n" );
	}
	else
	{
		return true;
	}
}

bool IKeyValuesDumpContextAsText::KvWriteIndent( int nIndentLevel )
{
	int numIndentBytes = ( nIndentLevel * 2 + 1 );
	char *pchIndent = ( char * ) stackalloc( numIndentBytes );
	memset( pchIndent, ' ', numIndentBytes - 1 );
	pchIndent[ numIndentBytes - 1 ] = 0;
	return KvWriteText( pchIndent );
}


bool CKeyValuesDumpContextAsDevMsg::KvBeginKey( KeyValues *pKey, int nIndentLevel )
{
	return IKeyValuesDumpContextAsText::KvBeginKey( pKey, nIndentLevel );
}

bool CKeyValuesDumpContextAsDevMsg::KvWriteText( char const *szText )
{
	if ( m_nDeveloperLevel > 0 )
	{
		//DevMsg( m_nDeveloperLevel, "%s", szText );
	}
	else
	{
		//Msg( "%s", szText );
	}
	return true;
}
