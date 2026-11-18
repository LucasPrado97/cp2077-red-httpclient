// Mac RVAs (game 2.5.0, Steam Mac build, UUID 7D7382D0-EADD-36F0-BA1C-CEBEC1D5BEBC) for the address hashes the SDK code of the three
// Red* plugins (RedData, RedFileSystem, RedHttpClient) uses and that the Mac SDK table (RED4ext.SDK-macos, RED4ext/Relocation-inl.hpp,
// macOSAddressTable) does not have. Common.cmake injects this file after the CGlobalFunction_ctor row of a generated copy of that
// header. The same file is in the three repositories (the union of what macos/hashscan.py finds in the three dylibs).
// Format: { hash, RVA },  // name. RVA is relative to the Cyberpunk2077 image base (Mac RVA = address - 0x100000000).
// Every row is copied unchanged from Codeware's macos/MacSdkRows.inc (branch ios, the build that runs on the phone); the evidence
// and the Windows twin of each row are in the comments there. Checked again for this port, read-only, against the 2.5.0 executable:
// every function row is an entry of LC_FUNCTION_STARTS in __TEXT, the two data rows are cells in __DATA.
// Never a 0 RVA: an unmapped hash goes to the trap (MacTrap.cpp).
        { 2547979664, 0x10C3934 },  // CClass_AssignDefaultValuesToProperties. void(CClass*, ScriptInstance). Windows InitializeScriptDefaultValues 0x147384.
        { 2257327441, 0x10C37D8 },  // CClass_InitializeProperties. void(CClass*, ScriptInstance). Windows InitializeScriptedProperties 0x147CB8.
        { 566894273, 0x10CF45C },  // CEnum_FromString. bool(CEnum*, instance, const char* text and a size): the string comes as a StringView in x2/x3. Windows 0x59B204 (the address table says 0x75DF94, which is wrong).
        { 68817558, 0x10CF304 },  // CEnum_ToString. bool(CEnum*, instance, CString&). Windows 0x599BFC.
        { 1087575974, 0x10CF100 },  // CEnum_Unserialize. bool(CEnum*, BaseStream*, instance, int64). Windows 0x1487A4.
        { 4175699423, 0x11e4da0 },  // IScriptable_sub_D8. void(this, unused, visitor). Windows 0x205FE10.
        { 114701897, 0x10b09a4 },  // ISerializable_sub_30. bool(this): true when there is no owning CResource. Shared by about 6700 vtables, never hook. Windows 0x2052B84.
        { 2293436409, 0x10B0860 },  // ISerializable_sub_40. bool(this, BaseStream*). Windows 0x148940.
        { 2715367827, 0x10B0C08 },  // ISerializable_sub_78. Mac: Handle(this, a1, a2 byte, a3, a4) with the Handle through x8 (the SDK signature models a hidden first argument). Windows 0x711A14.
        { 3978435378, 0x10b0b6c },  // ISerializable_sub_A0. The base default (GetType()->GetDefaultObject). The address table's Windows 0xB9A864 is a CResource override; the twin is 0x1247898.
        { 1508445968, 0x9D0E550 },  // JobDispatcher. Data: the cell that holds the dispatcher pointer. Windows .data 0x3438980.
        { 2621709954, 0x4DE3088 },  // JobDispatcher_DispatchJob. uint32_t(dispatcher, const JobInstance&, uint8_t, JobInternalHandle*, JobInternalHandle*). Not its tail target 0x9D8720. Windows 0x14299C.
        { 2655521326, 0x4DDF828 },  // JobHandle_dtor (ReleaseInternalHandle). Mac returns this. The caller-less twins are 0x9D498C and 0x9D49C4. Windows 0x1419A4.
        { 1862212562, 0x4DE2D7C },  // JobInternalHandle_Acquire. Mac JobInternalHandle*(ignored, const char* name, uintptr_t unk): one more argument than the SDK. Windows 0x142B20.
        { 3651996672, 0x4DDF354 },  // JobQueue_SyncWait. void(JobQueue*). Windows 0x141B10.
        { 2193429752, 0x4DDEF64 },  // JobQueue_ctor_FromParams. JobQueue*(JobQueue*, uint8_t, uint8_t, uint64_t). Windows 0x218B54.
        { 1525942140, 0x4DDF488 },  // JobQueue_dtor. Body. The game calls it through the thunk 0x9D46EC. Windows 0x142F88.
        { 3235774480, 0x4DF0D14 },  // MacJobSetLocalThreadParam. void(uint8_t). Mac-only id: the SDK writes TLS::Get()->jobParam, and TLS::Get() is null on Mac. 2468 callers, all around job bodies.
        { 2017202228, 0x99DC690 },  // ResourceLoader. Data: the cell holding the ResourceLoader pointer. Windows .data 0x3426E58.
