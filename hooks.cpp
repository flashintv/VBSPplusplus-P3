#include <Windows.h>
#include <tier1/utlbuffer.h>
#include <tier1/utlvector.h>
#include <tier1/utllinkedlist.h>
#include <tier1/keyvalues.h>
#include <tier0/memalloc.h>
#include <psapi.h>
#include <safetyhook.hpp>

// --------------

struct Vector
{
public:
	float x, y, z;
	Vector& operator+=( Vector& vec )
	{
		x += vec.x;
		y += vec.y;
		z += vec.z;
		return vec;
	}
	float& operator[]( int i )
	{
		return (&x)[i];
	}
};
typedef Vector QAngle;

typedef unsigned short GameLumpHandle_t;
typedef int GameLumpId_t;
enum
{
	GAMELUMP_TREE_PROPS = 'lvsp',
};
enum
{
	GAMELUMP_TREE_PROPS_VERSION = 1,
};

struct dgamelumpheader_t
{
	int lumpCount;
};

struct dgamelump_t
{
	GameLumpId_t	id;
	unsigned short	flags;
	unsigned short	version;
	int				fileofs;
	int				filelen;
};

struct GameLump_t
{
	GameLumpId_t	m_Id;
	unsigned short	m_Flags;
	unsigned short	m_Version;
	CUtlMemory< unsigned char >	m_Memory;
};

class CGameLump
{
public:
	//-----------------------------------------------------------------------------
	// Convert four-CC code to a handle	+ back
	//-----------------------------------------------------------------------------
	GameLumpHandle_t GetGameLumpHandle( GameLumpId_t id )
	{
		FOR_EACH_LL( m_GameLumps, i )
		{
			if ( m_GameLumps[i].m_Id == id )
				return i;
		}

		return InvalidGameLump();
	}
	GameLumpId_t GetGameLumpId( GameLumpHandle_t handle )
	{
		return m_GameLumps[handle].m_Id;
	}
	int	GetGameLumpFlags( GameLumpHandle_t handle )
	{
		return m_GameLumps[handle].m_Flags;
	}
	int GetGameLumpVersion( GameLumpHandle_t handle )
	{
		return m_GameLumps[handle].m_Version;
	}
	void ComputeGameLumpSizeAndCount( int& size, int& clumpCount )
	{
		// Figure out total size of the client lumps
		size = 0;
		clumpCount = 0;
		GameLumpHandle_t h;
		for ( h = FirstGameLump(); h != InvalidGameLump(); h = NextGameLump( h ) )
		{
			++clumpCount;
			size += GameLumpSize( h );
		}

		// Add on headers
		size += sizeof( dgamelumpheader_t ) + clumpCount * sizeof( dgamelump_t );
	}
	void SwapGameLump( GameLumpId_t id, int version, byte* dest, byte* src, int length )
	{
		Assert( false, "Unimplemented" );
	}


	//-----------------------------------------------------------------------------
	// Game lump accessor methods 
	//-----------------------------------------------------------------------------
	void* GetGameLump( GameLumpHandle_t id )
	{
		return m_GameLumps[id].m_Memory.Base();
	}
	int	GameLumpSize( GameLumpHandle_t id )
	{
		return m_GameLumps[id].m_Memory.NumAllocated();
	}


	//-----------------------------------------------------------------------------
	// Game lump iteration methods 
	//-----------------------------------------------------------------------------
	GameLumpHandle_t FirstGameLump()
	{
		return (m_GameLumps.Count()) ? m_GameLumps.Head() : InvalidGameLump();
	}
	GameLumpHandle_t NextGameLump( GameLumpHandle_t handle )
	{
		return (m_GameLumps.IsValidIndex( handle )) ? m_GameLumps.Next( handle ) : InvalidGameLump();
	}
	GameLumpHandle_t InvalidGameLump()
	{
		return 0xFFFF;
	}


	//-----------------------------------------------------------------------------
	// Game lump creation/destruction method
	//-----------------------------------------------------------------------------
	GameLumpHandle_t CreateGameLump( GameLumpId_t id, int size, int flags, int version )
	{
		Assert( GetGameLumpHandle( id ) == InvalidGameLump() );
		GameLumpHandle_t handle = m_GameLumps.AddToTail();
		m_GameLumps[handle].m_Id = id;
		m_GameLumps[handle].m_Flags = flags;
		m_GameLumps[handle].m_Version = version;
		m_GameLumps[handle].m_Memory.EnsureCapacity( size );
		return handle;
	}
	void DestroyGameLump( GameLumpHandle_t handle )
	{
		m_GameLumps.Remove( handle );
	}
	void DestroyAllGameLumps()
	{
		m_GameLumps.RemoveAll();
	}

private:
	CUtlLinkedList< GameLump_t, GameLumpHandle_t >	m_GameLumps;
};

typedef void* (*FSAllocFunc_t)(const char* pszFilename, unsigned nBytes);
class IBaseFileSystem
{
public:
	virtual int				Read( void* pOutput, int size, FileHandle_t file ) = 0;
	virtual int				Write( void const* pInput, int size, FileHandle_t file ) = 0;

	// if pathID is NULL, all paths will be searched for the file
	virtual FileHandle_t	Open( const char* pFileName, const char* pOptions, const char* pathID = 0 ) = 0;
	virtual void			Close( FileHandle_t file ) = 0;


	virtual void			Seek( FileHandle_t file, int pos, FileSystemSeek_t seekType ) = 0;
	virtual unsigned int	Tell( FileHandle_t file ) = 0;
	virtual unsigned int	Size( FileHandle_t file ) = 0;
	virtual unsigned int	Size( const char* pFileName, const char* pPathID = 0 ) = 0;

	virtual void			Flush( FileHandle_t file ) = 0;
	virtual bool			Precache( const char* pFileName, const char* pPathID = 0 ) = 0;

	virtual bool			FileExists( const char* pFileName, const char* pPathID = 0 ) = 0;
	virtual bool			IsFileWritable( char const* pFileName, const char* pPathID = 0 ) = 0;
	virtual bool			SetFileWritable( char const* pFileName, bool writable, const char* pPathID = 0 ) = 0;

	virtual long			GetFileTime( const char* pFileName, const char* pPathID = 0 ) = 0;

	//--------------------------------------------------------
	// Reads/writes files to utlbuffers. Use this for optimal read performance when doing open/read/close
	//--------------------------------------------------------
	virtual bool			ReadFile( const char* pFileName, const char* pPath, CUtlBuffer& buf, int nMaxBytes = 0, int nStartingByte = 0, FSAllocFunc_t pfnAlloc = NULL ) = 0;
	virtual bool			WriteFile( const char* pFileName, const char* pPath, CUtlBuffer& buf ) = 0;
	virtual bool			UnzipFile( const char* pFileName, const char* pPath, const char* pDestination ) = 0;
};

uint64_t FindSignature( const char* szModule, const char* szSignature )
{
	//CREDITS: learn_more
	#define INRANGE(x,a,b)  (x >= a && x <= b) 
	#define getBits( x )    (INRANGE((x&(~0x20)),'A','F') ? ((x&(~0x20)) - 'A' + 0xa) : (INRANGE(x,'0','9') ? x - '0' : 0))
	#define getByte( x )    (getBits(x[0]) << 4 | getBits(x[1]))

	MODULEINFO modInfo;
	GetModuleInformation( GetCurrentProcess(), GetModuleHandleA( szModule ), &modInfo, sizeof( MODULEINFO ) );
	uintptr_t startAddress = (uintptr_t)modInfo.lpBaseOfDll;
	uintptr_t endAddress = startAddress + modInfo.SizeOfImage;
	const char* pat = szSignature;
	uintptr_t firstMatch = 0;
	for ( uintptr_t pCur = startAddress; pCur < endAddress; pCur++ )
	{
		if ( !*pat ) return firstMatch;
		if ( *(PBYTE)pat == '\?' || *(BYTE*)pCur == getByte( pat ) )
		{
			if ( !firstMatch ) firstMatch = pCur;
			if ( !pat[2] ) return firstMatch;
			if ( *(PWORD)pat == '\?\?' || *(PBYTE)pat != '\?' ) pat += 3;
			else pat += 2;    //one ?
		}
		else
		{
			pat = szSignature;
			firstMatch = 0;
		}
	}
	return NULL;
}

// --------------

struct partial_entity_t* curentity = NULL;
IMemAlloc* g_pMemAlloc;
CGameLump* g_pGameLumps;
IBaseFileSystem* g_pFileSystem;
void* s_pPhysCollision;

// partial struct
struct epair_t
{
	epair_t* next;
	char* key;
	char* value;
};
struct partial_entity_t
{
	Vector		origin;
	int			firstbrush;
	int			numbrushes;
	epair_t*	epairs;
	char pad[32]; // areaportal stuff
};

struct TreePropLump_t
{
	Vector m_Origin;
	QAngle m_Angles;
	unsigned short m_Leaf;
	char m_szPlantType[35];
	unsigned char m_clrLeaf[3];
};

struct StaticPropBuild_t
{
	char const* m_pModelName;
	char const* m_pLightingOrigin;
	Vector	m_Origin;
	QAngle	m_Angles;
	int		m_Solid;
	int		m_Skin;
	int		m_Flags;
	float	m_FadeMinDist;
	float	m_FadeMaxDist;
	bool	m_FadesOut;
	float	m_flForcedFadeScale;
	unsigned short	m_nMinDXLevel;
	unsigned short	m_nMaxDXLevel;
	int		m_LightmapResolutionX;
	int		m_LightmapResolutionY;
};

struct StaticPropDictLump_t
{
	char	m_Name[128];		// model name
};

struct StaticPropLump_t
{
	Vector			m_Origin;
	QAngle			m_Angles;
	unsigned short	m_PropType;
	unsigned short	m_FirstLeaf;
	unsigned short	m_LeafCount;
	unsigned char	m_Solid;
	int				m_Skin;
	float			m_FadeMinDist;
	float			m_FadeMaxDist;
	Vector			m_LightingOrigin;
	float			m_flForcedFadeScale;
	unsigned short	m_nMinDXLevel;
	unsigned short	m_nMaxDXLevel;
	//	int				m_Lighting;			// index into the GAMELUMP_STATIC_PROP_LIGHTING lump
	int				m_Flags;
	unsigned short	m_nLightmapResolutionX;
	unsigned short	m_nLightmapResolutionY;
};

struct StaticPropLeafLump_t
{
	unsigned short	m_Leaf;
};

struct ModelCollisionLookup_t
{
	short m_Name;
	void* m_pCollide;
};

FORCEINLINE void VectorCopy( const Vector& src, Vector& dst )
{
	dst.x = src.x;
	dst.y = src.y;
	dst.z = src.z;
}

static CUtlVector<TreePropLump_t> s_TreePropLump;

CUtlVector<StaticPropDictLump_t>* s_pStaticPropDictLump; // "48 8B 05 ?? ?? ?? ?? 48 89 D9"
CUtlVector<StaticPropLump_t>* s_pStaticPropLump; // "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 C0 48 8D 3C C0"
CUtlVector<StaticPropLeafLump_t>* s_pStaticPropLeafLump; // "4C 89 25 ?? ?? ?? ?? 45 89 E8"
CUtlVector<int>* s_pLightingInfo; // "48 8B 15 ?? ?? ?? ?? 89 C0 49 89 C8"
partial_entity_t* pentities; // "4C 8D 2D ?? ?? ?? ?? 0F 8E 22 0B 00 00"

// "56 48 83 EC 20 48 89 CE FF"
KeyValues* (__fastcall* KeyValues_Alloc)(int); // int needs to be set to 64

// "56 48 83 EC 20 48 89 CE C7 01"
KeyValues* (__fastcall* KeyValues_KeyValues)(KeyValues*, const char*); // init

// "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 68 4C"
KeyValues* (__fastcall* KeyValues_LoadFromFile)(KeyValues*, void*, const char* path, const char* mod, int zero);

// "41 57 41 56 56 57 53 48 81 EC 30 02"
const char* (__fastcall* KeyValues_GetString)(KeyValues*, const char*, const char*);

// "41 57 41 56 41 55 41 54 56 57 53 48 81 EC 30"
KeyValues* (__fastcall* KeyValues_FindKey)(KeyValues*, const char*, bool);

// "55 41 56 56 57 53 48 81" - you have to supply your own cutlbuffer
void* (__fastcall* GetCollisionModel)(char const*, CUtlBuffer*);

// "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 78 0F 29 74 24 60 4D"
void (__fastcall* ComputeConvexHullLeaves_R)(int node, int depth, int* pNodeList, Vector const* mins, Vector const* maxs, Vector const* origin, QAngle const* angles, void* pCollide, CUtlVector<unsigned short>* leafList);

// "56 57 53 48 83 EC 20 44 89 CF 48"
CUtlBuffer* (__fastcall* CUtlBuffer_CUtlBuffer)(CUtlBuffer*);

// "41 56 56 57 55 53 48 83 EC 20 44 89 CF 44 89 C6"
GameLumpHandle_t (__fastcall* CGameLump_CreateGameLump)(CGameLump* ths, GameLumpId_t id, int size, int flags, int version);

const char* ValueForKey( partial_entity_t* ent, const char* key )
{
	for ( epair_t* ep = ent->epairs; ep; ep = ep->next )
		if ( !_stricmp( ep->key, key ) )
			return ep->value;
	return "";
}

void GetVectorForKey( partial_entity_t* ent, const char* key, Vector& vec )
{ 
	const char* k = ValueForKey( ent, key );
	// scanf into doubles, then assign, so it is vec_t size independent
	double	v1, v2, v3;
	v1 = v2 = v3 = 0;
	sscanf( k, "%lf %lf %lf", &v1, &v2, &v3 );
	vec[0] = v1;
	vec[1] = v2;
	vec[2] = v3;
}

int IntForKey( partial_entity_t* ent, const char* key )
{
	const char* k = ValueForKey( ent, key );
	return atol( k );
}

float FloatForKey( partial_entity_t* ent, const char* key )
{
	const char* k = ValueForKey( ent, key );
	return atof( k );
}

static void SetLumpData_TreeProps()
{
	GameLumpHandle_t handle = g_pGameLumps->GetGameLumpHandle( GAMELUMP_TREE_PROPS );
	if ( handle != g_pGameLumps->InvalidGameLump() )
		g_pGameLumps->DestroyGameLump( handle );

	int lumpsize = s_TreePropLump.Count() * sizeof( TreePropLump_t );
	int size = lumpsize + sizeof( int );

	handle = CGameLump_CreateGameLump( g_pGameLumps, GAMELUMP_TREE_PROPS, size, 0, GAMELUMP_TREE_PROPS_VERSION );
		//g_pGameLumps->CreateGameLump( GAMELUMP_TREE_PROPS, size, 0, GAMELUMP_TREE_PROPS_VERSION );

	// Serialize the data
	CUtlBuffer buf( g_pGameLumps->GetGameLump( handle ), size );
	buf.PutInt( s_TreePropLump.Count() );
	buf.Put( s_TreePropLump.Base(), lumpsize );
}

IBaseFileSystem* GetFileSystem()
{
	// 48 8B 15 ?? ?? ?? ?? C6 44 24 20 00 4C 8D 0D"
	// the 4 wildcards is the offset to g_pFileSystem (do + rsp to get the address of the pointer it)
	if ( !g_pFileSystem )
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "48 8B 15 ?? ?? ?? ?? C6 44 24 20 00 4C 8D 0D" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		g_pFileSystem = *reinterpret_cast<IBaseFileSystem**>(addr + 7 + rel);
	}
	return g_pFileSystem;
}

// Compound prop related function
static void GetCompoundSubProps( partial_entity_t* pent, KeyValues** out_pKVSubProps, int& out_nCount )
{
	static KeyValues* s_pKVCompoundObjectsRegistry = NULL;
	if ( !s_pKVCompoundObjectsRegistry )
	{
		s_pKVCompoundObjectsRegistry = KeyValues_Alloc( 64 );
		KeyValues_KeyValues( s_pKVCompoundObjectsRegistry, "CompoundObjectsRegistry" );
		KeyValues_LoadFromFile( s_pKVCompoundObjectsRegistry, GetFileSystem(), "scripts/compound_objects.txt", "MOD", NULL);
	}
	out_nCount = 0;

	const char* pszCompoundClass = ValueForKey(pent, "compound_class");
	if ( !s_pKVCompoundObjectsRegistry )
		return;

	KeyValues* pClass = KeyValues_FindKey( s_pKVCompoundObjectsRegistry, pszCompoundClass, false );
	if ( !pClass )
		return;

	FOR_EACH_SUBKEY( pClass, pChild )
	{
		const char* pClass = KeyValues_GetString( pChild, "class", "" );
		if ( !strcmp( pClass, "prop_static" ) || !strcmp( pClass, "static_prop" ) )
		{
			out_pKVSubProps[out_nCount++] = pChild;
		}
	}
}

static void ComputeStaticPropLeaves( void* pCollide, Vector const& origin,
	QAngle const& angles, CUtlVector<unsigned short>& leafList )
{
	// Compute an axis-aligned bounding box for the collide
	Vector mins, maxs;

	// "48 8B 0D ?? ?? ?? ?? 4C 8B 11 4C 89 7C 24 28" + index 23 = vtable func we want
	if ( !s_pPhysCollision )
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "48 8B 0D ?? ?? ?? ?? 4C 8B 11 4C 89 7C 24 28" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		s_pPhysCollision = *reinterpret_cast<void**>(addr + 7 + rel);
	}

	typedef void (__fastcall* CollideGetAABB)(void*, Vector*, Vector*, const void*, const Vector&, const QAngle&);
	((CollideGetAABB)((*((void***)s_pPhysCollision))[23]))(s_pPhysCollision, &mins, &maxs, pCollide, origin, angles);

	// Find all leaves that intersect with the bounds
	int tempNodeList[1024];
	ComputeConvexHullLeaves_R( 0, 0, tempNodeList, &mins, &maxs,
		&origin, &angles, pCollide, &leafList );
}

static bool ComputeLightingOrigin( StaticPropBuild_t const& build, Vector& lightingOrigin )
{
	for ( int i = s_pLightingInfo->Count(); --i >= 0; )
	{
		int entIndex = s_pLightingInfo->Element(i);

		// Check against all lighting info entities
		char const* pTargetName = ValueForKey( &pentities[entIndex], "targetname" );
		if ( !strcmp( pTargetName, build.m_pLightingOrigin ) )
		{
			GetVectorForKey( &pentities[entIndex], "origin", lightingOrigin );
			return true;
		}
	}

	return false;
}

static int AddStaticPropDictLump( char const* pModelName )
{
	StaticPropDictLump_t dictLump;
	strncpy( dictLump.m_Name, pModelName, 128 );

	for ( int i = s_pStaticPropDictLump->Count(); --i >= 0; )
	{
		if ( !memcmp( &s_pStaticPropDictLump->Element(i), &dictLump, sizeof( dictLump ) ) )
			return i;
	}

	return s_pStaticPropDictLump->AddToTail( dictLump );
}

static int AddStaticPropToLump( StaticPropBuild_t const& build )
{
	// Get the collision model
	CUtlBuffer* buf = (CUtlBuffer*)alloca( 256 );
	CUtlBuffer_CUtlBuffer( buf );
	void* pConvexHull = GetCollisionModel( build.m_pModelName, buf );
	if ( !pConvexHull )
		return -1;

	// Compute the leaves the static prop's convex hull hits
	CUtlVector< unsigned short > leafList;
	ComputeStaticPropLeaves( pConvexHull, build.m_Origin, build.m_Angles, leafList );

	if ( !leafList.Count() )
	{
		printf( "Static prop %s outside the map (%.2f, %.2f, %.2f)\n", build.m_pModelName, build.m_Origin.x, build.m_Origin.y, build.m_Origin.z );
		return -1;
	}
	// Insert an element into the lump data...
	int i = s_pStaticPropLump->AddToTail();
	StaticPropLump_t& propLump = s_pStaticPropLump->Element(i);
	propLump.m_PropType = AddStaticPropDictLump( build.m_pModelName );
	VectorCopy( build.m_Origin, propLump.m_Origin );
	VectorCopy( build.m_Angles, propLump.m_Angles );
	propLump.m_FirstLeaf = s_pStaticPropLump->Count();
	propLump.m_LeafCount = leafList.Count();
	propLump.m_Solid = build.m_Solid;
	propLump.m_Skin = build.m_Skin;
	propLump.m_Flags = build.m_Flags;
	if ( build.m_FadesOut )
	{
		propLump.m_Flags |= 1;
	}
	propLump.m_FadeMinDist = build.m_FadeMinDist;
	propLump.m_FadeMaxDist = build.m_FadeMaxDist;
	propLump.m_flForcedFadeScale = build.m_flForcedFadeScale;
	propLump.m_nMinDXLevel = build.m_nMinDXLevel;
	propLump.m_nMaxDXLevel = build.m_nMaxDXLevel;

	if ( build.m_pLightingOrigin && *build.m_pLightingOrigin )
	{
		if ( ComputeLightingOrigin( build, propLump.m_LightingOrigin ) )
		{
			propLump.m_Flags |= 2;
		}
	}

	// VBSP++ has code here related to warning about vertex lighting for static props
	// and disabling it using STATIC_PROP_NO_PER_VERTEX_LIGHTING, but because of 
	// compound props never actually being static; I'm not implementing it.

	propLump.m_nLightmapResolutionX = build.m_LightmapResolutionX;
	propLump.m_nLightmapResolutionY = build.m_LightmapResolutionY;

	// Add the leaves to the leaf lump
	for ( int j = 0; j < leafList.Count(); ++j )
	{
		StaticPropLeafLump_t insert;
		insert.m_Leaf = leafList.Element(j);
		s_pStaticPropLeafLump->AddToTail( insert );
	}
	return s_pStaticPropLump->Count() - 1;
}

// --------------

class InitMemAlloc
{
public:
	InitMemAlloc() {
		// Shady shit, surprisingly tier0.dll is loaded by the time we are executing stuff.
		g_pMemAlloc = *(decltype(g_pMemAlloc)*)(GetProcAddress( GetModuleHandleA( "tier0.dll" ), "g_pMemAlloc" ));
	}
};
static InitMemAlloc m;

safetyhook::InlineHook o_EmitStaticProps;
void EmitStaticProps()
{
	curentity = &pentities[0];
	o_EmitStaticProps.ccall();
	SetLumpData_TreeProps();
}

safetyhook::MidHook o_HookEntityIndex;
void HookEntityIndex( safetyhook::Context& ctx )
{
	curentity = &pentities[(int)ctx.rax];
}

safetyhook::MidHook o_CheckStaticProps_EmitStaticProps;
void CheckStaticProps_EmitStaticProps( safetyhook::Context& ctx )
{
	const char* pEntity = (const char*)(ctx.rax);
	if ( !V_strcmp( pEntity, "p3_prop_compound" ) )
	{
		KeyValues* pSubPropKVs[32];
		int nSubProps = 0;
		GetCompoundSubProps( curentity, pSubPropKVs, nSubProps );

		if ( nSubProps )
		{
			Vector parent_origin;
			QAngle parent_angles;
			GetVectorForKey( curentity, "origin", parent_origin );
			GetVectorForKey( curentity, "angles", parent_angles );

			for ( int j = 0; j < nSubProps; ++j )
			{
				KeyValues* pSubPropKV = pSubPropKVs[j];

				StaticPropBuild_t build;

				const char* pOriginStr = KeyValues_GetString( pSubPropKV, "origin", "" );
				sscanf( pOriginStr, "%f %f %f", &build.m_Origin.x, &build.m_Origin.y, &build.m_Origin.z );
				const char* pAnglesStr = KeyValues_GetString( pSubPropKV, "angles", "" );
				sscanf( pAnglesStr, "%f %f %f", &build.m_Angles.x, &build.m_Angles.y, &build.m_Angles.z );
				build.m_Origin += parent_origin;
				build.m_Angles += parent_angles;

				build.m_pModelName = KeyValues_GetString( pSubPropKV, "model", "" );
				build.m_Solid = pSubPropKV->GetInt( "solid", 6 );
				build.m_Skin = pSubPropKV->GetInt( "skin", 0 );
				build.m_FadeMaxDist = pSubPropKV->GetFloat( "fademaxdist", 0.0f );

				build.m_Flags = 0;
				if ( pSubPropKV->GetInt( "ignorenormals", 0 ) == 1 )
				{
					build.m_Flags |= 8;
				}
				if ( pSubPropKV->GetInt( "disableshadows", 0 ) == 1 )
				{
					build.m_Flags |= 16;
				}
				if ( pSubPropKV->GetInt( "disablevertexlighting", 0 ) == 1 )
				{
					build.m_Flags |= 64;
				}
				if ( pSubPropKV->GetInt( "disableselfshadowing", 0 ) == 1 )
				{
					build.m_Flags |= 128;
				}
				if ( pSubPropKV->GetInt( "screenspacefade", 0 ) == 1 )
				{
					build.m_Flags |= 32;
				}

				// p3 doesn't support it
				build.m_LightmapResolutionX = 0;
				build.m_LightmapResolutionY = 0;

				if ( IntForKey( curentity, "enablelightbounce" ) == 1 )
				{
					build.m_Flags |= 2;
				}

				build.m_flForcedFadeScale = pSubPropKV->GetFloat( "fadescale", 1.0f );

				build.m_FadesOut = (build.m_FadeMaxDist > 0);
				build.m_pLightingOrigin = KeyValues_GetString( pSubPropKV, "lightingorigin", "" );
				if ( build.m_FadesOut )
				{
					build.m_FadeMinDist = pSubPropKV->GetFloat( "fademindist", -1.0f );
					if ( build.m_FadeMinDist < 0 )
					{
						build.m_FadeMinDist = build.m_FadeMaxDist;
					}
				}
				else
				{
					build.m_FadeMinDist = 0;
				}
				build.m_nMinDXLevel = pSubPropKV->GetInt( "mindxlevel" );
				build.m_nMaxDXLevel = pSubPropKV->GetInt( "maxdxlevel" );
				AddStaticPropToLump( build );
			}
		}
	}
	else if ( !V_strcmp( pEntity, "p3_prop_tree" ) )
	{
		StaticPropBuild_t build;

		GetVectorForKey( curentity, "origin", build.m_Origin );
		GetVectorForKey( curentity, "angles", build.m_Angles );
		build.m_pModelName = ValueForKey( curentity, "model" );
		build.m_Solid = IntForKey( curentity, "solid" );
		build.m_Skin = IntForKey( curentity, "skin" );
		build.m_FadeMaxDist = FloatForKey( curentity, "fademaxdist" );
		build.m_Flags = 0;//IntForKey( &curentity, "spawnflags" ) & STATIC_PROP_WC_MASK;
		if ( IntForKey( curentity, "ignorenormals" ) == 1 )
		{
			build.m_Flags |= 8;
		}
		if ( IntForKey( curentity, "disableshadows" ) == 1 )
		{
			build.m_Flags |= 16;
		}
		if ( IntForKey( curentity, "disablevertexlighting" ) == 1 )
		{
			build.m_Flags |= 64;
		}
		if ( IntForKey( curentity, "disableselfshadowing" ) == 1 )
		{
			build.m_Flags |= 128;
		}

		if ( IntForKey( curentity, "screenspacefade" ) == 1 )
		{
			build.m_Flags |= 32;
		}

		// p3 doesn't support it
		build.m_LightmapResolutionX = 0;
		build.m_LightmapResolutionY = 0;

		if ( IntForKey( curentity, "enablelightbounce" ) == 1 )
		{
			build.m_Flags |= 2;
		}

		const char* pKey = ValueForKey( curentity, "fadescale" );
		if ( pKey && pKey[0] )
		{
			build.m_flForcedFadeScale = FloatForKey( curentity, "fadescale" );
		}
		else
		{
			build.m_flForcedFadeScale = 1;
		}
		build.m_FadesOut = (build.m_FadeMaxDist > 0);
		build.m_pLightingOrigin = ValueForKey( curentity, "lightingorigin" );
		if ( build.m_FadesOut )
		{
			build.m_FadeMinDist = FloatForKey( curentity, "fademindist" );
			if ( build.m_FadeMinDist < 0 )
			{
				build.m_FadeMinDist = build.m_FadeMaxDist;
			}
		}
		else
		{
			build.m_FadeMinDist = 0;
		}
		build.m_nMinDXLevel = (unsigned short)IntForKey( curentity, "mindxlevel" );
		build.m_nMaxDXLevel = (unsigned short)IntForKey( curentity, "maxdxlevel" );

		int nLumpPos = AddStaticPropToLump( build );
		// It's always a prop tree - removed the check!
		const char* pPlantType = ValueForKey( curentity, "plant_type" );

		int rgb[3] = { 255, 255, 255 };
		const char* pLeafColor = ValueForKey( curentity, "leaves_color" );
		sscanf( pLeafColor, "%d %d %d", &rgb[0], &rgb[1], &rgb[2] );

		TreePropLump_t treeLump;
		treeLump.m_Origin = build.m_Origin;
		treeLump.m_Angles = build.m_Angles;
		treeLump.m_Leaf = (nLumpPos < 0) ? 0 : nLumpPos;
		treeLump.m_clrLeaf[0] = rgb[0];
		treeLump.m_clrLeaf[1] = rgb[1];
		treeLump.m_clrLeaf[2] = rgb[2];
		V_strncpy( treeLump.m_szPlantType, pPlantType, sizeof( treeLump.m_szPlantType ) );

		s_TreePropLump.AddToTail( treeLump );

		// strip this ent from the .bsp file
		curentity->epairs = 0;
	}
}

// --------------

void InitializeHooks()
{
	g_pGameLumps = NULL;
	g_pFileSystem = NULL;
	s_pPhysCollision = NULL;

	//
	KeyValues_Alloc = (decltype(KeyValues_Alloc))FindSignature( "vbspplusplus.exe", "56 48 83 EC 20 48 89 CE FF" );
	KeyValues_KeyValues = (decltype(KeyValues_KeyValues))FindSignature( "vbspplusplus.exe", "56 48 83 EC 20 48 89 CE C7 01" );
	KeyValues_LoadFromFile = (decltype(KeyValues_LoadFromFile))FindSignature( "vbspplusplus.exe", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 68 4C" );
	KeyValues_GetString = (decltype(KeyValues_GetString))FindSignature( "vbspplusplus.exe", "41 57 41 56 56 57 53 48 81 EC 30 02" );
	KeyValues_FindKey = (decltype(KeyValues_FindKey))FindSignature( "vbspplusplus.exe", "41 57 41 56 41 55 41 54 56 57 53 48 81 EC 30" );
	GetCollisionModel = (decltype(GetCollisionModel))FindSignature( "vbspplusplus.exe", "55 41 57 41 56 56 57 53 48 81" );
	ComputeConvexHullLeaves_R = (decltype(ComputeConvexHullLeaves_R))FindSignature( "vbspplusplus.exe", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 78 0F 29 74 24 60 4D" );
	CUtlBuffer_CUtlBuffer = (decltype(CUtlBuffer_CUtlBuffer))FindSignature( "vbspplusplus.exe", "56 57 53 48 83 EC 20 44 89 CF 48" );
	CGameLump_CreateGameLump = (decltype(CGameLump_CreateGameLump))FindSignature( "vbspplusplus.exe", "41 56 56 57 55 53 48 83 EC 20 44 89 CF 44 89 C6" );

	// Function EmitStaticProps
	// "41 57 41 56 41 55 41 54 56 57 55 53 B8 C8"
	o_EmitStaticProps = safetyhook::create_inline( 
		FindSignature( "vbspplusplus.exe", "41 57 41 56 41 55 41 54 56 57 55 53 B8 C8" ), 
		EmitStaticProps );

	/*
	48 8D 3D AD C4 04 26    lea     rdi, xmmword_1660808D0
	48 89 F9                mov     rcx, rdi

	"48 8D 3D ?? ?? ?? ?? 48 89 F9 BA 70 72 70 73"
	
	+ 7 = RSP
	RSP + ?? ?? ?? ?? = g_GameLumps
	*/
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "48 8D 3D ?? ?? ?? ?? 48 89 F9 BA 70 72 70 73" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		g_pGameLumps = reinterpret_cast<CGameLump*>(addr + 7 + rel);
	}

	/*
	48 63 C8                 movsxd  rcx, eax
	48 C1 E1 06              shl     rcx, 6
	4C 01 E9                 add     rcx, r13
	48 89 DA                 mov     rdx, rbx
	E8 91 DE 01 00           call    ValueForKey
	48 89 C6                 mov     rsi, rax
	
	"48 63 C8 48 C1 E1 06 4C 01 E1 4C 89 F2"

	get eax for index
	*/
	o_HookEntityIndex = safetyhook::create_mid(
		FindSignature( "vbspplusplus.exe", "48 63 C8 48 C1 E1 06 4C 01 E1 4C 89 F2" ),
		HookEntityIndex );

	/*
	// rax is the classname
	48 89 C1                mov     rcx, rax        ; Str1
	48 8D 15 9F C6 0A 00    lea     rdx, aStaticProp ; "static_prop"
	E8 0F 89 0A 00          call    strcmp
	85 C0                   test    eax, eax
	74 3B                   jz      short loc_140033940

	"48 89 C1 48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 74 3B"
	*/
	o_CheckStaticProps_EmitStaticProps = safetyhook::create_mid( 
		FindSignature( "vbspplusplus.exe", "48 89 C1 48 8D 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 74 3B" ), 
		CheckStaticProps_EmitStaticProps );

	// filesystem was previously set here, but then i realized its not available instantly
	
	// physcollision was previously set here, but then i realized its not 
	// available instantly, so its moved to slightly after emitstaticprops 
	// starts executing in the ComputeStaticPropLeaves

	// "48 8B 05 ?? ?? ?? ?? 48 89 D9"
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "48 8B 05 ?? ?? ?? ?? 48 89 D9" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		s_pStaticPropDictLump = reinterpret_cast<CUtlVector<StaticPropDictLump_t>*>(addr + 7 + rel);
	}

	// "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 C0 48 6B F8 64"
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "48 8D 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 C0 48 6B F8 64" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		s_pStaticPropLump = reinterpret_cast<CUtlVector<StaticPropLump_t>*>(addr + 7 + rel);
	}

	// "4C 8B 2D ?? ?? ?? ?? 44 89 E3"
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "4C 8B 2D ?? ?? ?? ?? 44 89 E3" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		s_pStaticPropLeafLump = reinterpret_cast<CUtlVector<StaticPropLeafLump_t>*>(addr + 7 + rel);
	}

	// "48 8B 15 ?? ?? ?? ?? 89 C0 49 89 C8"
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "48 8B 15 ?? ?? ?? ?? 89 C0 49 89 C8" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		s_pLightingInfo = reinterpret_cast<CUtlVector<int>*>(addr + 7 + rel);
	}

	// "4C 8D 2D ?? ?? ?? ?? 0F 8E 22 0B 00 00"
	{
		uint64_t addr = FindSignature( "vbspplusplus.exe", "4C 8D 25 ?? ?? ?? ?? 0F 8E CD 0D 00 00" );
		int32_t rel = *reinterpret_cast<int32_t*>(addr + 3);
		pentities = reinterpret_cast<partial_entity_t*>(addr + 7 + rel);
	}
}