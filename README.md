# VBSP++P3 (Unfinished)
>DLL based patch for VBSP++ that added support for Postal 3 specific features from P3's VBSP.

>*This code was uploaded for archival purposes.*

>[!IMPORTANT]
>This patch was only tested on the version from 12th April, 2026!

>[!CAUTION]
>Was tested - it *runs successfully* (with a couple of **model related errors**, which might be caused by structs not being accurate to ficool2's) but **does not work in-game and crashes on startup**, most likely caused by the old models and their format that Postal 3 uses (which slightly differs from retail; being out of date by newer standards/versions), and to compile with VBSP++; you need x64 libraries which P3 obviously does not provide.

------------

#### Included features:
- [x] Added code that adds compound props (`p3_prop_compound`) to the static prop list (from testing it does not does alter anything visually or gameplay-wise, could be a leftover feature).
- [x] Added code for trees (`p3_prop_tree`). It creates a game lump with tree data to be used within Postal 3 code at runtime to render leaves.

------------

#### The future for map compilers for Postal 3:
Instead of this project - I will most likely continue working on my own standalone VBSP based on the 2007 version which does work with Postal 3 binaries while also containing the Postal 3 specific features. It will be announced when I optimize it more, and finish fixing small bugs with my custom StudioRender and MaterialSystem, which will be packaged alongside the VBSP (and to be used for best compile times with VVIS++ and VRAD++).

------------

#### Credits:
- Valve Software *for their library headers (https://github.com/ValveSoftware/GameNetworkingSockets/)*
- ficool2 *for Tools++ (https://ficool2.github.io/HammerPlusPlus-Website/tools.html)*
- Cursey and all the contributors *for safetyhook (https://github.com/cursey/safetyhook)*
- learn_more *for the signature scanner function (https://www.unknowncheats.me/forum/c-and-c/77419-findpattern.html#post650040)*
- Kizoky *for supplying me with Postal 3 specific headers (https://www.moddb.com/mods/postal-iii-ultrapatch)*
