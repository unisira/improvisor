#include <improvisor.h>
#include <arch/memory.h>
#include <pdb/pdb.h>
#include <section.h>

// Microsoft's MSF header magic
#define MSF_MAGIC ("Microsoft C/C++ MSF 7.00\r\n\x1A\x44\x53\x00\x00\x00")

// A parsed PDB entry containing all important streams
typedef struct _PDB_ENTRY
{
	LIST_ENTRY Links;
	// Hash of the name of the PDB file
	UINT64 NameHash;
	// The TPI stream (index 2)
	PVOID TpiStream;
	// The DBI stream (index 3)
	PVOID DbiStream;
	// The IPI stream (index 4)
	PVOID IpiStream;
	// The public symbol stream
	PVOID PubSymStream;
	// The hash stream
	PVOID HashStream;
} PDB_ENTRY, *PPDB_ENTRY;

// Raw list of PDB entries
static PPDB_ENTRY sPdbEntriesRaw = NULL;
// Head of the list of PDB entries
static PPDB_ENTRY sPdbEntriesHead = NULL;

typedef struct _MSF_SUPER_BLOCK
{
	UCHAR Magic[32];
	UINT32 BlockSize;
	UINT32 FreeBlockMapBlock;
	UINT32 BlockCount;
	UINT32 DirectorySize;
	UINT32 Unknown;
	UINT32 BlockMapBlock;
} MSF_SUPER_BLOCK, *PMSF_SUPER_BLOCK;

typedef struct _MSF_STREAM_DIRECTORY
{
	UINT32 StreamCount;
	UINT32 StreamSizes[1 /* StreamCount */];
	// Immediately following `StreamSizes`:
	// UINT32 StreamBlocks[StreamCount][]
} MSF_STREAM_DIRECTORY, * PMSF_STREAM_DIRECTORY;

typedef struct _DBI_HEADER
{
	INT32	VersionSignature;
	UINT32	VersionHeader;
	UINT32	Age;
	UINT16	GlobalStreamIndex;
	UINT16	BuildNumber;
	UINT16	PublicStreamIndex;
	UINT16	PdbDllVersion;
	UINT16	SymRecordStream;
	UINT16	PdbDllRbld;
	INT32	ModInfoSize;
	INT32	SectionContributionSize;
	INT32	SectionMapSize;
	INT32	SourceInfoSize;
	INT32	TypeServerSize;
	UINT32	MFCTypeServerIndex;
	INT32	OptionalDbgHeaderSize;
	INT32	ECSubstreamSize;
	UINT16	Flags;
	UINT16	Machine;
	UINT32	Padding;
} DBI_HEADER, *PDBI_HEADER;

typedef struct _TPI_HEADER
{
	UINT32 Version;
	UINT32 HeaderSize;
	UINT32 TypeIndexBegin;
	UINT32 TypeIndexEnd;
	UINT32 TypeRecordBytes;

	UINT16 HashStreamIndex;
	UINT16 HashAuxStreamIndex;
	UINT32 HashKeySize;
	UINT32 NumHashBuckets;

	INT32 HashValueBufferOffset;
	UINT32 HashValueBufferLength;

	INT32 IndexOffsetBufferOffset;
	UINT32 IndexOffsetBufferLength;

	INT32 HashAdjBufferOffset;
	UINT32 HashAdjBufferLength;
} TPI_HEADER, *PTPI_HEADER;

typedef enum _TPI_LEAF_RECORD_KIND
{
	LF_MODIFIER_16t = 0x0001,
	LF_POINTER_16t = 0x0002,
	LF_ARRAY_16t = 0x0003,
	LF_CLASS_16t = 0x0004,
	LF_STRUCTURE_16t = 0x0005,
	LF_UNION_16t = 0x0006,
	LF_ENUM_16t = 0x0007,
	LF_PROCEDURE_16t = 0x0008,
	LF_MFUNCTION_16t = 0x0009,
	LF_VTSHAPE = 0x000a,
	LF_COBOL0_16t = 0x000b,
	LF_COBOL1 = 0x000c,
	LF_BARRAY_16t = 0x000d,
	LF_LABEL = 0x000e,
	LF_NULL = 0x000f,
	LF_NOTTRAN = 0x0010,
	LF_DIMARRAY_16t = 0x0011,
	LF_VFTPATH_16t = 0x0012,
	LF_PRECOMP_16t = 0x0013,       // not referenced from symbol
	LF_ENDPRECOMP = 0x0014,       // not referenced from symbol
	LF_OEM_16t = 0x0015,       // oem definable type string
	LF_TYPESERVER_ST = 0x0016,       // not referenced from symbol

	// leaf indices starting records but referenced only from type records

	LF_SKIP_16t = 0x0200,
	LF_ARGLIST_16t = 0x0201,
	LF_DEFARG_16t = 0x0202,
	LF_LIST = 0x0203,
	LF_FIELDLIST_16t = 0x0204,
	LF_DERIVED_16t = 0x0205,
	LF_BITFIELD_16t = 0x0206,
	LF_METHODLIST_16t = 0x0207,
	LF_DIMCONU_16t = 0x0208,
	LF_DIMCONLU_16t = 0x0209,
	LF_DIMVARU_16t = 0x020a,
	LF_DIMVARLU_16t = 0x020b,
	LF_REFSYM = 0x020c,

	LF_BCLASS_16t = 0x0400,
	LF_VBCLASS_16t = 0x0401,
	LF_IVBCLASS_16t = 0x0402,
	LF_ENUMERATE_ST = 0x0403,
	LF_FRIENDFCN_16t = 0x0404,
	LF_INDEX_16t = 0x0405,
	LF_MEMBER_16t = 0x0406,
	LF_STMEMBER_16t = 0x0407,
	LF_METHOD_16t = 0x0408,
	LF_NESTTYPE_16t = 0x0409,
	LF_VFUNCTAB_16t = 0x040a,
	LF_FRIENDCLS_16t = 0x040b,
	LF_ONEMETHOD_16t = 0x040c,
	LF_VFUNCOFF_16t = 0x040d,

	// 32-bit type index versions of leaves, all have the 0x1000 bit set
	//
	LF_TI16_MAX = 0x1000,

	LF_MODIFIER = 0x1001,
	LF_POINTER = 0x1002,
	LF_ARRAY_ST = 0x1003,
	LF_CLASS_ST = 0x1004,
	LF_STRUCTURE_ST = 0x1005,
	LF_UNION_ST = 0x1006,
	LF_ENUM_ST = 0x1007,
	LF_PROCEDURE = 0x1008,
	LF_MFUNCTION = 0x1009,
	LF_COBOL0 = 0x100a,
	LF_BARRAY = 0x100b,
	LF_DIMARRAY_ST = 0x100c,
	LF_VFTPATH = 0x100d,
	LF_PRECOMP_ST = 0x100e,       // not referenced from symbol
	LF_OEM = 0x100f,       // oem definable type string
	LF_ALIAS_ST = 0x1010,       // alias (typedef) type
	LF_OEM2 = 0x1011,       // oem definable type string

	// leaf indices starting records but referenced only from type records

	LF_SKIP = 0x1200,
	LF_ARGLIST = 0x1201,
	LF_DEFARG_ST = 0x1202,
	LF_FIELDLIST = 0x1203,
	LF_DERIVED = 0x1204,
	LF_BITFIELD = 0x1205,
	LF_METHODLIST = 0x1206,
	LF_DIMCONU = 0x1207,
	LF_DIMCONLU = 0x1208,
	LF_DIMVARU = 0x1209,
	LF_DIMVARLU = 0x120a,

	LF_BCLASS = 0x1400,
	LF_VBCLASS = 0x1401,
	LF_IVBCLASS = 0x1402,
	LF_FRIENDFCN_ST = 0x1403,
	LF_INDEX = 0x1404,
	LF_MEMBER_ST = 0x1405,
	LF_STMEMBER_ST = 0x1406,
	LF_METHOD_ST = 0x1407,
	LF_NESTTYPE_ST = 0x1408,
	LF_VFUNCTAB = 0x1409,
	LF_FRIENDCLS = 0x140a,
	LF_ONEMETHOD_ST = 0x140b,
	LF_VFUNCOFF = 0x140c,
	LF_NESTTYPEEX_ST = 0x140d,
	LF_MEMBERMODIFY_ST = 0x140e,
	LF_MANAGED_ST = 0x140f,

	// Types w/ SZ names

	LF_ST_MAX = 0x1500,

	LF_TYPESERVER = 0x1501,       // not referenced from symbol
	LF_ENUMERATE = 0x1502,
	LF_ARRAY = 0x1503,
	LF_CLASS = 0x1504,
	LF_STRUCTURE = 0x1505,
	LF_UNION = 0x1506,
	LF_ENUM = 0x1507,
	LF_DIMARRAY = 0x1508,
	LF_PRECOMP = 0x1509,       // not referenced from symbol
	LF_ALIAS = 0x150a,       // alias (typedef) type
	LF_DEFARG = 0x150b,
	LF_FRIENDFCN = 0x150c,
	LF_MEMBER = 0x150d,
	LF_STMEMBER = 0x150e,
	LF_METHOD = 0x150f,
	LF_NESTTYPE = 0x1510,
	LF_ONEMETHOD = 0x1511,
	LF_NESTTYPEEX = 0x1512,
	LF_MEMBERMODIFY = 0x1513,
	LF_MANAGED = 0x1514,
	LF_TYPESERVER2 = 0x1515,

	LF_STRIDED_ARRAY = 0x1516,    // same as LF_ARRAY, but with stride between adjacent elements
	LF_HLSL = 0x1517,
	LF_MODIFIER_EX = 0x1518,
	LF_INTERFACE = 0x1519,
	LF_BINTERFACE = 0x151a,
	LF_VECTOR = 0x151b,
	LF_MATRIX = 0x151c,

	LF_VFTABLE = 0x151d,      // a virtual function table
	LF_ENDOFLEAFRECORD = LF_VFTABLE,

	LF_TYPE_LAST,                    // one greater than the last type record
	LF_TYPE_MAX = LF_TYPE_LAST - 1,

	LF_FUNC_ID = 0x1601,    // global func ID
	LF_MFUNC_ID = 0x1602,    // member func ID
	LF_BUILDINFO = 0x1603,    // build info: tool, version, command line, src/pdb file
	LF_SUBSTR_LIST = 0x1604,    // similar to LF_ARGLIST, for list of sub strings
	LF_STRING_ID = 0x1605,    // string ID

	LF_UDT_SRC_LINE = 0x1606,    // source and line on where an UDT is defined
									 // only generated by compiler

	LF_UDT_MOD_SRC_LINE = 0x1607,    // module, source and line on where an UDT is defined
																	 // only generated by linker

	LF_ID_LAST,                      // one greater than the last ID record
	LF_ID_MAX = LF_ID_LAST - 1,

	LF_NUMERIC = 0x8000,
	LF_CHAR = 0x8000,
	LF_SHORT = 0x8001,
	LF_USHORT = 0x8002,
	LF_LONG = 0x8003,
	LF_ULONG = 0x8004,
	LF_REAL32 = 0x8005,
	LF_REAL64 = 0x8006,
	LF_REAL80 = 0x8007,
	LF_REAL128 = 0x8008,
	LF_QUADWORD = 0x8009,
	LF_UQUADWORD = 0x800a,
	LF_REAL48 = 0x800b,
	LF_COMPLEX32 = 0x800c,
	LF_COMPLEX64 = 0x800d,
	LF_COMPLEX80 = 0x800e,
	LF_COMPLEX128 = 0x800f,
	LF_VARSTRING = 0x8010,

	LF_OCTWORD = 0x8017,
	LF_UOCTWORD = 0x8018,

	LF_DECIMAL = 0x8019,
	LF_DATE = 0x801a,
	LF_UTF8STRING = 0x801b,

	LF_REAL16 = 0x801c,

	LF_PAD0 = 0xf0,
	LF_PAD1 = 0xf1,
	LF_PAD2 = 0xf2,
	LF_PAD3 = 0xf3,
	LF_PAD4 = 0xf4,
	LF_PAD5 = 0xf5,
	LF_PAD6 = 0xf6,
	LF_PAD7 = 0xf7,
	LF_PAD8 = 0xf8,
	LF_PAD9 = 0xf9,
	LF_PAD10 = 0xfa,
	LF_PAD11 = 0xfb,
	LF_PAD12 = 0xfc,
	LF_PAD13 = 0xfd,
	LF_PAD14 = 0xfe,
	LF_PAD15 = 0xff,
} TPI_LEAF_RECORD_KIND, * PTPI_LEAF_RECORD_KIND;

typedef union _TPI_TYPE_INDEX
{
	UINT32 Value;
	
	struct
	{
		UINT32 Subkind : 4;
		UINT32 Kind : 4;
		UINT32 Mode : 4;
		UINT32 Complex : 4;
		UINT32 Unused : 16;
	};
} TPI_TYPE_INDEX, *PTPI_TYPE_INDEX;

typedef struct _TPI_INDEX_OFFSET_ENTRY
{
	TPI_TYPE_INDEX Ti;
	UINT32 Offset;
} TPI_INDEX_OFFSET_ENTRY, * PTPI_INDEX_OFFSET_ENTRY;

// Base TPI leaf record
typedef struct _TPI_LEAF_RECORD
{
	// Length of the record excluding TPI_LEAF_RECORD::RecordLength
	UINT16 Length;
	// The kind of record (LF_*)
	UINT16 Kind;
} TPI_LEAF_RECORD, *PTPI_LEAF_RECORD;

typedef struct _TPI_STRUCTURE_PROPERTIES
{
	UINT16 Packed : 1;				// true if structure is packed
	UINT16 CtorPresent : 1;			// true if constructors or destructors present
	UINT16 OverloadedOps : 1;		// true if overloaded operators present
	UINT16 IsNested : 1;			// true if this is a nested class
	UINT16 ContainsNested : 1;		// true if this class contains nested types
	UINT16 OverloadedAssignOp : 1;	// true if overloaded assignment (=)
	UINT16 opcast : 1;				// true if casting methods
	UINT16 ForwardRef : 1;			// true if forward reference (incomplete defn)
	UINT16 Scoped : 1;				// scoped definition
	UINT16 HasUniqueName : 1;		// true if there is a decorated name following the regular name
	UINT16 Sealed : 1;				// true if class cannot be used as a base class
	UINT16 HFA : 2;					// HFA_e
	UINT16 Intrinsic : 1;			// true if class is an intrinsic type (e.g. __m128d)
	UINT16 MoCom : 2;				// MOCOM_UDT_e
} TPI_STRUCTURE_PROPERTIES, * PTPI_STRUCTURE_PROPERTIES;

typedef struct _TPI_STRUCTURE_LEAF_RECORD
{
	TPI_LEAF_RECORD Head;
	UINT16 Count;							// count of number of elements in class
	TPI_STRUCTURE_PROPERTIES Properties;	// property attribute field (prop_t)
	TPI_TYPE_INDEX Field;					// type index of LF_FIELD descriptor list
	TPI_TYPE_INDEX Derived;					// type index of derived from list if not zero
	TPI_TYPE_INDEX VShape;					// type index of vshape table for this class
	UINT8 Data[1];							// data describing length of structure in bytes and name
} TPI_STRUCTURE_LEAF_RECORD, * PTPI_STRUCTURE_LEAF_RECORD;

typedef struct _TPI_FIELD_LIST_LEAF_RECORD
{
	TPI_LEAF_RECORD Head;
	UINT8 Data[1]; // Field list sub list
} TPI_FIELD_LIST_LEAF_RECORD, * PTPI_FIELD_LIST_LEAF_RECORD;

typedef struct _TPI_FIELD_ATTRIBUTES {
	UINT16 Access : 2;		// access protection CV_access_t
	UINT16 MProp : 3;		// method properties CV_methodprop_t
	UINT16 Pseudo : 1;		// compiler generated fcn and does not exist
	UINT16 NoInherit : 1;   // true if class cannot be inherited
	UINT16 NoConstruct : 1; // true if class cannot be constructed
	UINT16 CompGenx : 1;    // compiler generated fcn and does exist
	UINT16 Sealed : 1;		// true if method cannot be overridden
	UINT16 Unused : 6;		// unused
} TPI_FIELD_ATTRIBUTES, * PTPI_FIELD_ATTRIBUTES;

typedef struct _TPI_MEMBER_LEAF_RECORD
{
	TPI_LEAF_RECORD Head;
	TPI_FIELD_ATTRIBUTES Attr;
	TPI_TYPE_INDEX Type;
	UINT8 Data[1];
} TPI_MEMBER_LEAF_RECORD, * PTPI_MEMBER_LEAF_RECORD;

typedef struct _CV_SYMTYPE_HEADER
{
	UINT16 Length;
	UINT16 Type;
} CV_SYMTYPE_HEADER, *PCV_SYMTYPE_HEADER;

typedef struct _CV_DATASYM32
{
	CV_SYMTYPE_HEADER Header;
	UINT32 Offs;
	UINT16 Segment;
	UINT16 TypeIndex;
	UINT8 Name[1];
} CV_DATASYM32, *PCV_DATASYM32;

typedef enum _CV_SYMENUM {
	S_COMPILE = 0x0001,  // Compile flags symbol
	S_REGISTER_16t = 0x0002,  // Register variable
	S_CONSTANT_16t = 0x0003,  // constant symbol
	S_UDT_16t = 0x0004,  // User defined type
	S_SSEARCH = 0x0005,  // Start Search
	S_END = 0x0006,  // Block, procedure, "with" or thunk end
	S_SKIP = 0x0007,  // Reserve symbol space in $$Symbols table
	S_CVRESERVE = 0x0008,  // Reserved symbol for CV internal use
	S_OBJNAME_ST = 0x0009,  // path to object file name
	S_ENDARG = 0x000a,  // end of argument/return list
	S_COBOLUDT_16t = 0x000b,  // special UDT for cobol that does not symbol pack
	S_MANYREG_16t = 0x000c,  // multiple register variable
	S_RETURN = 0x000d,  // return description symbol
	S_ENTRYTHIS = 0x000e,  // description of this pointer on entry

	S_BPREL16 = 0x0100,  // BP-relative
	S_LDATA16 = 0x0101,  // Module-local symbol
	S_GDATA16 = 0x0102,  // Global data symbol
	S_PUB16 = 0x0103,  // a public symbol
	S_LPROC16 = 0x0104,  // Local procedure start
	S_GPROC16 = 0x0105,  // Global procedure start
	S_THUNK16 = 0x0106,  // Thunk Start
	S_BLOCK16 = 0x0107,  // block start
	S_WITH16 = 0x0108,  // with start
	S_LABEL16 = 0x0109,  // code label
	S_CEXMODEL16 = 0x010a,  // change execution model
	S_VFTABLE16 = 0x010b,  // address of virtual function table
	S_REGREL16 = 0x010c,  // register relative address

	S_BPREL32_16t = 0x0200,  // BP-relative
	S_LDATA32_16t = 0x0201,  // Module-local symbol
	S_GDATA32_16t = 0x0202,  // Global data symbol
	S_PUB32_16t = 0x0203,  // a public symbol (CV internal reserved)
	S_LPROC32_16t = 0x0204,  // Local procedure start
	S_GPROC32_16t = 0x0205,  // Global procedure start
	S_THUNK32_ST = 0x0206,  // Thunk Start
	S_BLOCK32_ST = 0x0207,  // block start
	S_WITH32_ST = 0x0208,  // with start
	S_LABEL32_ST = 0x0209,  // code label
	S_CEXMODEL32 = 0x020a,  // change execution model
	S_VFTABLE32_16t = 0x020b,  // address of virtual function table
	S_REGREL32_16t = 0x020c,  // register relative address
	S_LTHREAD32_16t = 0x020d,  // local thread storage
	S_GTHREAD32_16t = 0x020e,  // global thread storage
	S_SLINK32 = 0x020f,  // static link for MIPS EH implementation

	S_LPROCMIPS_16t = 0x0300,  // Local procedure start
	S_GPROCMIPS_16t = 0x0301,  // Global procedure start

	// if these ref symbols have names following then the names are in ST format
	S_PROCREF_ST = 0x0400,  // Reference to a procedure
	S_DATAREF_ST = 0x0401,  // Reference to data
	S_ALIGN = 0x0402,  // Used for page alignment of symbols

	S_LPROCREF_ST = 0x0403,  // Local Reference to a procedure
	S_OEM = 0x0404,  // OEM defined symbol

	// sym records with 32-bit types embedded instead of 16-bit
	// all have 0x1000 bit set for easy identification
	// only do the 32-bit target versions since we don't really
	// care about 16-bit ones anymore.
	S_TI16_MAX = 0x1000,

	S_REGISTER_ST = 0x1001,  // Register variable
	S_CONSTANT_ST = 0x1002,  // constant symbol
	S_UDT_ST = 0x1003,  // User defined type
	S_COBOLUDT_ST = 0x1004,  // special UDT for cobol that does not symbol pack
	S_MANYREG_ST = 0x1005,  // multiple register variable
	S_BPREL32_ST = 0x1006,  // BP-relative
	S_LDATA32_ST = 0x1007,  // Module-local symbol
	S_GDATA32_ST = 0x1008,  // Global data symbol
	S_PUB32_ST = 0x1009,  // a public symbol (CV internal reserved)
	S_LPROC32_ST = 0x100a,  // Local procedure start
	S_GPROC32_ST = 0x100b,  // Global procedure start
	S_VFTABLE32 = 0x100c,  // address of virtual function table
	S_REGREL32_ST = 0x100d,  // register relative address
	S_LTHREAD32_ST = 0x100e,  // local thread storage
	S_GTHREAD32_ST = 0x100f,  // global thread storage

	S_LPROCMIPS_ST = 0x1010,  // Local procedure start
	S_GPROCMIPS_ST = 0x1011,  // Global procedure start

	S_FRAMEPROC = 0x1012,  // extra frame and proc information
	S_COMPILE2_ST = 0x1013,  // extended compile flags and info

	// new symbols necessary for 16-bit enumerates of IA64 registers
	// and IA64 specific symbols

	S_MANYREG2_ST = 0x1014,  // multiple register variable
	S_LPROCIA64_ST = 0x1015,  // Local procedure start (IA64)
	S_GPROCIA64_ST = 0x1016,  // Global procedure start (IA64)

	// Local symbols for IL
	S_LOCALSLOT_ST = 0x1017,  // local IL sym with field for local slot index
	S_PARAMSLOT_ST = 0x1018,  // local IL sym with field for parameter slot index

	S_ANNOTATION = 0x1019,  // Annotation string literals

	// symbols to support managed code debugging
	S_GMANPROC_ST = 0x101a,  // Global proc
	S_LMANPROC_ST = 0x101b,  // Local proc
	S_RESERVED1 = 0x101c,  // reserved
	S_RESERVED2 = 0x101d,  // reserved
	S_RESERVED3 = 0x101e,  // reserved
	S_RESERVED4 = 0x101f,  // reserved
	S_LMANDATA_ST = 0x1020,
	S_GMANDATA_ST = 0x1021,
	S_MANFRAMEREL_ST = 0x1022,
	S_MANREGISTER_ST = 0x1023,
	S_MANSLOT_ST = 0x1024,
	S_MANMANYREG_ST = 0x1025,
	S_MANREGREL_ST = 0x1026,
	S_MANMANYREG2_ST = 0x1027,
	S_MANTYPREF = 0x1028,  // Index for type referenced by name from metadata
	S_UNAMESPACE_ST = 0x1029,  // Using namespace

	// Symbols w/ SZ name fields. All name fields contain utf8 encoded strings.
	S_ST_MAX = 0x1100,  // starting point for SZ name symbols

	S_OBJNAME = 0x1101,  // path to object file name
	S_THUNK32 = 0x1102,  // Thunk Start
	S_BLOCK32 = 0x1103,  // block start
	S_WITH32 = 0x1104,  // with start
	S_LABEL32 = 0x1105,  // code label
	S_REGISTER = 0x1106,  // Register variable
	S_CONSTANT = 0x1107,  // constant symbol
	S_UDT = 0x1108,  // User defined type
	S_COBOLUDT = 0x1109,  // special UDT for cobol that does not symbol pack
	S_MANYREG = 0x110a,  // multiple register variable
	S_BPREL32 = 0x110b,  // BP-relative
	S_LDATA32 = 0x110c,  // Module-local symbol
	S_GDATA32 = 0x110d,  // Global data symbol
	S_PUB32 = 0x110e,  // a public symbol (CV internal reserved)
	S_LPROC32 = 0x110f,  // Local procedure start
	S_GPROC32 = 0x1110,  // Global procedure start
	S_REGREL32 = 0x1111,  // register relative address
	S_LTHREAD32 = 0x1112,  // local thread storage
	S_GTHREAD32 = 0x1113,  // global thread storage

	S_LPROCMIPS = 0x1114,  // Local procedure start
	S_GPROCMIPS = 0x1115,  // Global procedure start
	S_COMPILE2 = 0x1116,  // extended compile flags and info
	S_MANYREG2 = 0x1117,  // multiple register variable
	S_LPROCIA64 = 0x1118,  // Local procedure start (IA64)
	S_GPROCIA64 = 0x1119,  // Global procedure start (IA64)
	S_LOCALSLOT = 0x111a,  // local IL sym with field for local slot index
	S_SLOT = S_LOCALSLOT,  // alias for LOCALSLOT
	S_PARAMSLOT = 0x111b,  // local IL sym with field for parameter slot index

	// symbols to support managed code debugging
	S_LMANDATA = 0x111c,
	S_GMANDATA = 0x111d,
	S_MANFRAMEREL = 0x111e,
	S_MANREGISTER = 0x111f,
	S_MANSLOT = 0x1120,
	S_MANMANYREG = 0x1121,
	S_MANREGREL = 0x1122,
	S_MANMANYREG2 = 0x1123,
	S_UNAMESPACE = 0x1124,  // Using namespace

	// ref symbols with name fields
	S_PROCREF = 0x1125,  // Reference to a procedure
	S_DATAREF = 0x1126,  // Reference to data
	S_LPROCREF = 0x1127,  // Local Reference to a procedure
	S_ANNOTATIONREF = 0x1128,  // Reference to an S_ANNOTATION symbol
	S_TOKENREF = 0x1129,  // Reference to one of the many MANPROCSYM's

	// continuation of managed symbols
	S_GMANPROC = 0x112a,  // Global proc
	S_LMANPROC = 0x112b,  // Local proc

	// short, light-weight thunks
	S_TRAMPOLINE = 0x112c,  // trampoline thunks
	S_MANCONSTANT = 0x112d,  // constants with metadata type info

	// native attributed local/parms
	S_ATTR_FRAMEREL = 0x112e,  // relative to virtual frame ptr
	S_ATTR_REGISTER = 0x112f,  // stored in a register
	S_ATTR_REGREL = 0x1130,  // relative to register (alternate frame ptr)
	S_ATTR_MANYREG = 0x1131,  // stored in >1 register

	// Separated code (from the compiler) support
	S_SEPCODE = 0x1132,

	S_LOCAL_2005 = 0x1133,  // defines a local symbol in optimized code
	S_DEFRANGE_2005 = 0x1134,  // defines a single range of addresses in which symbol can be evaluated
	S_DEFRANGE2_2005 = 0x1135,  // defines ranges of addresses in which symbol can be evaluated

	S_SECTION = 0x1136,  // A COFF section in a PE executable
	S_COFFGROUP = 0x1137,  // A COFF group
	S_EXPORT = 0x1138,  // A export

	S_CALLSITEINFO = 0x1139,  // Indirect call site information
	S_FRAMECOOKIE = 0x113a,  // Security cookie information

	S_DISCARDED = 0x113b,  // Discarded by LINK /OPT:REF (experimental, see richards)

	S_COMPILE3 = 0x113c,  // Replacement for S_COMPILE2
	S_ENVBLOCK = 0x113d,  // Environment block split off from S_COMPILE2

	S_LOCAL = 0x113e,  // defines a local symbol in optimized code
	S_DEFRANGE = 0x113f,  // defines a single range of addresses in which symbol can be evaluated
	S_DEFRANGE_SUBFIELD = 0x1140,           // ranges for a subfield

	S_DEFRANGE_REGISTER = 0x1141,           // ranges for en-registered symbol
	S_DEFRANGE_FRAMEPOINTER_REL = 0x1142,   // range for stack symbol.
	S_DEFRANGE_SUBFIELD_REGISTER = 0x1143,  // ranges for en-registered field of symbol
	S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE = 0x1144, // range for stack symbol span valid full scope of function body, gap might apply.
	S_DEFRANGE_REGISTER_REL = 0x1145, // range for symbol address as register + offset.

	// S_PROC symbols that reference ID instead of type
	S_LPROC32_ID = 0x1146,
	S_GPROC32_ID = 0x1147,
	S_LPROCMIPS_ID = 0x1148,
	S_GPROCMIPS_ID = 0x1149,
	S_LPROCIA64_ID = 0x114a,
	S_GPROCIA64_ID = 0x114b,

	S_BUILDINFO = 0x114c, // build information.
	S_INLINESITE = 0x114d, // inlined function callsite.
	S_INLINESITE_END = 0x114e,
	S_PROC_ID_END = 0x114f,

	S_DEFRANGE_HLSL = 0x1150,
	S_GDATA_HLSL = 0x1151,
	S_LDATA_HLSL = 0x1152,

	S_FILESTATIC = 0x1153,

#if defined(CC_DP_CXX) && CC_DP_CXX

	S_LOCAL_DPC_GROUPSHARED = 0x1154, // DPC groupshared variable
	S_LPROC32_DPC = 0x1155, // DPC local procedure start
	S_LPROC32_DPC_ID = 0x1156,
	S_DEFRANGE_DPC_PTR_TAG = 0x1157, // DPC pointer tag definition range
	S_DPC_SYM_TAG_MAP = 0x1158, // DPC pointer tag value to symbol record map

#endif // CC_DP_CXX

	S_ARMSWITCHTABLE = 0x1159,
	S_CALLEES = 0x115a,
	S_CALLERS = 0x115b,
	S_POGODATA = 0x115c,
	S_INLINESITE2 = 0x115d,      // extended inline site information

	S_HEAPALLOCSITE = 0x115e,    // heap allocation site

	S_MOD_TYPEREF = 0x115f,      // only generated at link time

	S_REF_MINIPDB = 0x1160,      // only generated at link time for mini PDB
	S_PDBMAP = 0x1161,      // only generated at link time for mini PDB

	S_GDATA_HLSL32 = 0x1162,
	S_LDATA_HLSL32 = 0x1163,

	S_GDATA_HLSL32_EX = 0x1164,
	S_LDATA_HLSL32_EX = 0x1165,

	S_RECTYPE_MAX,               // one greater than last
	S_RECTYPE_LAST = S_RECTYPE_MAX - 1,
	S_RECTYPE_PAD = S_RECTYPE_MAX + 0x100 // Used *only* to verify symbol record types so that current PDB code can potentially read
								// future PDBs (assuming no format change, etc).

} CV_SYMENUM;

SIZE_T
PdbSearchFieldList(
	_In_ PPDB_ENTRY Pdb,
	_In_ PTPI_FIELD_LIST_LEAF_RECORD Lr,
	_In_ FNV1A Member
);

SIZE_T
PdbFindMemberOffsetEx(
	_In_ PPDB_ENTRY Pdb,
	_In_ FNV1A Structure,
	_In_ FNV1A Member
);

NTSTATUS
PdbReserveEntries(
	_In_ SIZE_T Count
)
/*++
Routine Description:
	This function parses a PDB and stores all information necessary to extract type and symbol information in a PDB entry
--*/
{
	sPdbEntriesRaw = ImpAllocateHostNpPool(sizeof(PDB_ENTRY) * Count);
	if (sPdbEntriesRaw == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	sPdbEntriesHead = sPdbEntriesRaw;

	for (SIZE_T i = 0; i < Count; i++)
	{
		PPDB_ENTRY CurrEntry = sPdbEntriesRaw + i;

		CurrEntry->Links.Flink = i < Count - 1 ? &(CurrEntry + 1)->Links : NULL;
		CurrEntry->Links.Blink = i > 0 ? &(CurrEntry - 1)->Links : NULL;
	}

	return STATUS_SUCCESS;
}

NTSTATUS
PdbAllocateEntry(
	_In_ PPDB_ENTRY* Entry
)
/*++
Routine Description:
	Consumes one entry from the PDB entry list
--*/
{
	if (sPdbEntriesHead->Links.Flink == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	*Entry = sPdbEntriesHead;

	sPdbEntriesHead = sPdbEntriesHead->Links.Flink;

	return STATUS_SUCCESS;
}

PPDB_ENTRY
PdbFindEntry(
	_In_ FNV1A Name
)
/*++
Routine Description:
	Finds a PDB entry using `Name`
--*/
{
	PPDB_ENTRY CurrEntry = sPdbEntriesHead;
	while (CurrEntry != NULL)
	{
		if (CurrEntry->NameHash == Name)
			return CurrEntry;

		CurrEntry = CurrEntry->Links.Blink;
	}

	return NULL;
}

BOOLEAN
MsfIsMagicValid(
	_In_ PMSF_SUPER_BLOCK SuperBlock
)
/*++
Routine Description:
	Makes sure the MSF superblock header magic is correct
--*/
{
	return RtlCompareMemory(MSF_MAGIC, SuperBlock->Magic, sizeof(SuperBlock->Magic)) == 0;
}

PMSF_STREAM_DIRECTORY
MsfParseStreamDirectory(
	_In_ PMSF_SUPER_BLOCK SuperBlock
)
/*++
Routine Description:
	Extracts the MSF stream directory using the MSF superblock
--*/
{
	if (MsfIsMagicValid(SuperBlock))
		return NULL;

	const SIZE_T SdSize = SuperBlock->DirectorySize;
	const SIZE_T BkSize = SuperBlock->BlockSize;

	// How many blocks does the stream directory lie in
	const SIZE_T BkCount = (SdSize + BkSize - 1) / BkSize;
	// Get the address of the array of blocks
	const PUINT32 BlockIds = RVA_PTR(SuperBlock, SuperBlock->BlockMapBlock * BkSize);

	// Allocate a buffer big enough to hold the whole stream directory
	PMSF_STREAM_DIRECTORY StreamDir = ExAllocatePoolWithTag(NonPagedPool, BkCount * BkSize, POOL_TAG);
	if (StreamDir == NULL)
		return NULL;

	for (SIZE_T i = 0; i < BkCount; i++)
	{
		const PCHAR Block = RVA_PTR(SuperBlock, BlockIds[i] * BkSize);
		// Copy this block into the SD buffer
		RtlCopyMemory(RVA_PTR(StreamDir, i * BkSize), Block, BkSize);
	}

	return StreamDir;
}

PVOID*
MsfParseStreams(
	_In_ PMSF_SUPER_BLOCK SuperBlock
)
/*++
Routine Description:
	Extracts all streams from an MSF stream directory
--*/
{
	const PMSF_STREAM_DIRECTORY Sd = MsfParseStreamDirectory(SuperBlock);
	if (Sd == NULL)
		return NULL;

	// Allocate the streams buffer, reserve 1 for null entry
	PVOID* Streams = ExAllocatePoolWithTag(NonPagedPool, sizeof(PVOID) * (Sd->StreamCount + 1), POOL_TAG);
	if (Streams == NULL)
		return NULL;

	RtlZeroMemory(Streams, sizeof(PVOID) * (Sd->StreamCount + 1));

	const SIZE_T BkSize = SuperBlock->BlockSize;
	// The stream blocks array follows immediately after the variable length StreamSize
	const PUINT32 SdBlocks = RVA_PTR(&Sd->StreamSizes, sizeof(UINT32) * Sd->StreamCount);

	// Keep record of previous stream block count
	SIZE_T UsedBlocks = 0;

	for (SIZE_T i = 0; i < Sd->StreamCount; i++)
	{
		const SIZE_T StreamSize = Sd->StreamSizes[i];
		if (StreamSize == 0xFFFFFFFF)
			continue;

		const SIZE_T StreamBlocks = (StreamSize + BkSize - 1) / BkSize;

		// Allocate a buffer big enough to hold the whole stream directory
		PVOID Stream = ImpAllocateNpPool(StreamBlocks * BkSize);
		if (Stream == NULL)
			return NULL;

		for (SIZE_T j = 0; j < StreamBlocks; j++)
		{
			const PCHAR Block = RVA_PTR(SuperBlock, SdBlocks[UsedBlocks + j] * BkSize);
			// Copy this block into the stream buffer
			RtlCopyMemory(RVA_PTR(Stream, j * BkSize), Block, BkSize);
		}

		// Add this stream to the stream list
		Streams[i] = Stream;

		UsedBlocks += StreamBlocks;
	}

	// Add a null terminal entry so we don't need to return the count
	Streams[Sd->StreamCount] = NULL;

	// Stream directory is now useless
	ExFreePoolWithTag(Sd, POOL_TAG);

	return Streams;
}

SIZE_T
PdbExtractVar(
	PUINT8 Data,
	PUINT64 Number
)
/*++
Routine Description:
	Extracts a variable length value's size
--*/
{
	PUSHORT Leaf = Data;

	if (*Leaf < LF_NUMERIC)
	{
		*Number = *Leaf;
		// No more data than this short is stored here
		return sizeof(USHORT);
	}

	switch (*Leaf)
	{
	case LF_CHAR: 
		*Number = *RVA_PTR_T(CHAR, Leaf, sizeof(USHORT));
		return sizeof(USHORT) + sizeof(CHAR);
	case LF_SHORT: 
		*Number = *RVA_PTR_T(SHORT, Leaf, sizeof(USHORT));
		return sizeof(USHORT) + sizeof(SHORT);
	case LF_USHORT: 
		*Number = *RVA_PTR_T(USHORT, Leaf, sizeof(USHORT));
		return sizeof(USHORT) + sizeof(USHORT);
	case LF_LONG: 
		*Number = *RVA_PTR_T(LONG, Leaf, sizeof(USHORT));
		return sizeof(USHORT) + sizeof(LONG);
	case LF_ULONG: 
		*Number = *RVA_PTR_T(ULONG, Leaf, sizeof(USHORT));
		return sizeof(USHORT) + sizeof(ULONG);
	case LF_QUADWORD: 
		*Number = *RVA_PTR_T(INT64, Leaf, sizeof(USHORT));
		return sizeof(USHORT) + sizeof(INT64);
	case LF_UQUADWORD: 
		*Number = *RVA_PTR_T(UINT64, Leaf, sizeof(USHORT));
		return sizeof(USHORT) + sizeof(UINT64);
	}
}

PTPI_LEAF_RECORD
PdbLookupTypeIndex(
	_In_ PPDB_ENTRY Pdb,
	_In_ TPI_TYPE_INDEX Ti
)
/*++
Routine Description:
	O(Log(n)) lookup of complex type indices to type record using the IndexOffsetBuffer and a linear search
--*/
{
	// Not a complex type, return NULL
	if (Ti.Complex == 0)
		return NULL;

	PTPI_HEADER Tpi = Pdb->TpiStream;

	PTPI_INDEX_OFFSET_ENTRY IndexOffsetBuffer = RVA_PTR(Pdb->HashStream, Tpi->IndexOffsetBufferOffset);

	PTPI_INDEX_OFFSET_ENTRY Curr = NULL;

	SIZE_T Ih = Tpi->IndexOffsetBufferLength / sizeof(TPI_INDEX_OFFSET_ENTRY), Il = 0;
	while (Ih > Il)
	{
		SIZE_T Im = (Ih + Il) / 2;
		// Search from the middle of the buffer
		Curr = &IndexOffsetBuffer[Im];

		if (Ti.Value < Curr->Ti.Value)
			Ih = Im;
		else if (Ti.Value > Curr->Ti.Value)
			Il = Im + 1;
		// Only happens when we find an exact match
		else
			return RVA_PTR(Tpi, sizeof(TPI_HEADER) + Curr->Offset);
	}

	// Should be impossible
	if (Curr == NULL)
		return NULL;

	// NOTE: Sometimes the closest entry's TI is greater than `Ti`
	while (Curr->Ti.Value > Ti.Value)
		Curr = Curr - 1;

	SIZE_T i = Ti.Value - Curr->Ti.Value;

	PTPI_LEAF_RECORD Lr = RVA_PTR(Tpi, sizeof(TPI_HEADER) + Curr->Offset);
	// Move forward `i` times from the closest to our type index
	while (Lr->Length != 0 && i-- != 0)
		Lr = RVA_PTR(Lr, Lr->Length + sizeof(UINT16));

	// Wasn't found, invalid TI
	if (Lr->Length == 0)
		return NULL;

	return Lr;
}

SIZE_T
PdbSearchStructure(
	_In_ PPDB_ENTRY Pdb,
	_In_ PTPI_STRUCTURE_LEAF_RECORD Lr,
	_In_ FNV1A Member
)
/*++
Routine Description:
	Searches a LF_STRUCTURE's Field type record for `Member`
--*/
{
	if (Lr->Field.Value != 0)
		// The type index in Field will always point to a LF_FIELDLIST type record
		return PdbSearchFieldList(Pdb, PdbLookupTypeIndex(Pdb, Lr->Field), Member);
	else
		ImpLog("[PDB #%08X] Structure/Class LR with ForwardRef == 0 has no field TI...\n");

	return -1;
}

SIZE_T
PdbSearchFieldList(
	_In_ PPDB_ENTRY Pdb,
	_In_ PTPI_FIELD_LIST_LEAF_RECORD Lr,
	_In_ FNV1A Member
)
/*++
Routine Description:
	Iterate over the members of an LF_FIELDLIST type record looking for an entry with name equal to `Member`
--*/
{
	SIZE_T Size = 0;
	while (Size < Lr->Head.Length - sizeof(UINT16))
	{
		// Skip all LF_PAD entries
		while (*RVA_PTR_T(UCHAR, Lr->Data, Size) >= LF_PAD0)
			Size++;

		// The - sizeof(UINT16) is a hack, length isn't included in the type records
		PTPI_LEAF_RECORD Curr = RVA_PTR(Lr->Data, Size - sizeof(UINT16));
		switch (Curr->Kind)
		{
		// TODO: Check if i need to search LF_BCLASS type for member
		// TODO: Handle LF_NESTEDTYPE
		case LF_MEMBER:
		{
			PTPI_MEMBER_LEAF_RECORD MemberLr = Curr;

			UINT64 CurrOffset = 0;
			UINT64 OffsetSize = PdbExtractVar(MemberLr->Data, &CurrOffset);
			// Get the name of the current member
			LPCSTR Name = RVA_PTR(MemberLr->Data, OffsetSize);
			
			// Check if this member's name's hash equals `Member`
			if (FNV1A_HASH(Name) == Member)
			{
				return CurrOffset;
			}
			// If this type is complex, get its leaf record and check if it needs to be searched
			else if (MemberLr->Type.Complex != 0)
			{
				PTPI_LEAF_RECORD TypeLr = PdbLookupTypeIndex(Pdb, MemberLr->Type);

				if (TypeLr->Kind == LF_STRUCTURE || TypeLr->Kind == LF_CLASS)
				{
					UINT64 Size = 0;
					// Get the name of the structure after the data size
					LPCSTR Name = RVA_PTR(Lr->Data, PdbExtractVar(Lr->Data, &Size));
 
					SIZE_T Offset = PdbFindMemberOffsetEx(Pdb, FNV1A_HASH(Name), Member);
					if (Offset != -1)
						return CurrOffset + Offset;
				}
			}

			SIZE_T NameSz = 0;
			while (*Name++ != '\x00')
				NameSz++;

			Size += sizeof(TPI_MEMBER_LEAF_RECORD) - sizeof(UINT16) + OffsetSize + NameSz;
		} break;
		default:
		{
			ImpLog("[PDB #%08X] Unknown LR Kind %i...\n", Pdb->NameHash, Curr->Kind);
			// Return -1 because we can't handle this
			return -1;
		} break;
		}
	}

	// No LF_MEMBER entry with Name == Member
	return -1;
}

PDB_SYMBOL_RESULT
PdbFindSymbol(
	_In_ FNV1A Pdb,
	_In_ FNV1A Hash,
	_In_ UINT64 Flags
)
/*++
Routine Description:
	Linearly searches the DBI stream of `Pdb` for a symbol with a name whose hash that matches `Name`
--*/
{
	PDB_SYMBOL_RESULT Res = {
		.Offset = -1,
		.Segment = -1,
	};

	// TODO: Log all errors
	PPDB_ENTRY Entry = PdbFindEntry(Pdb);
	if (Entry == NULL)
		return Res;

	PCV_SYMTYPE_HEADER Record = Entry->PubSymStream;

	while (Record->Length != 0)
	{
		// TODO: Look at LF_*_ST, these also follow the structure type
		if (Record->Type != S_PUB32)
			goto next;

		PCV_DATASYM32 Sym = Record;

		SIZE_T Size = 0;
		// Get the name of the structure after the data size
		LPCSTR Name = RVA_PTR(Sym->Name, PdbExtractVar(Sym->Name, &Size));

		// Check if this member's name's hash equals `Member`
		if (FNV1A_HASH(Name) == Hash)
		{
			Res.Offset = Sym->Offs;
			Res.Segment = Sym->Segment;

			return Res;
		}

	next:
		Record = RVA_PTR(Record, Record->Length + sizeof(UINT16));
	}

	// TODO: Implement me
	return Res;
}

SIZE_T
PdbFindMemberOffsetEx(
	_In_ PPDB_ENTRY Pdb,
	_In_ FNV1A Structure,
	_In_ FNV1A Member
)
{
	PTPI_LEAF_RECORD Record = RVA_PTR(Pdb->TpiStream, sizeof(TPI_HEADER));

	while (Record->Length != 0)
	{
		// TODO: Look at LF_*_ST, these also follow the structure type
		if (Record->Kind != LF_STRUCTURE &&
			Record->Kind != LF_CLASS)
			goto next;

		PTPI_STRUCTURE_LEAF_RECORD Lr = Record;

		// Forward references don't have a Field TI
		if (Lr->Properties.ForwardRef)
			goto next;

		UINT64 Size = 0;
		// Get the name of the structure after the data size
		LPCSTR Name = RVA_PTR(Lr->Data, PdbExtractVar(Lr->Data, &Size));

		// If we have a match, search the field list for `Member`
		if (FNV1A_HASH(Name) != Structure)
			goto next;

		// If we make it here, this should be our structure
		SIZE_T Offset = PdbSearchStructure(Pdb, Lr, Member);
		if (Offset != -1)
			return Offset;

	next:
		Record = RVA_PTR(Record, Record->Length + sizeof(UINT16));
	}

	return -1;
}

SIZE_T
PdbFindMemberOffset(
	_In_ FNV1A Pdb,
	_In_ FNV1A Structure,
	_In_ FNV1A Member
)
/*++
Routine Description:
	Searches the TPI stream of `Pdb` for a type named `Structure` and returns the offset of `Member` from within that struct
--*/
{
	// TODO: Log all errors
	PPDB_ENTRY Entry = PdbFindEntry(Pdb);
	if (Entry == NULL)
		return -1;

	return PdbFindMemberOffsetEx(Entry, Structure, Member);
}

VOID
PdbParseMSF(
	_In_ PPDB_ENTRY Entry,
	_In_ PVOID Pdb
)
/*++
Routine Description:
	This function parses the MSF headers for a PDB file and stores all the necessary streams and frees any others.
--*/
{
	PVOID* Streams = MsfParseStreams(Pdb);

	Entry->TpiStream = Streams[2];
	Entry->DbiStream = Streams[3];
	Entry->IpiStream = Streams[4];

	// Get the DBI header and store the public symbol stream
	PDBI_HEADER DbiHeader = Entry->DbiStream;
	
	Entry->PubSymStream = Streams[DbiHeader->PublicStreamIndex];

	// Get the TPI header and store the hash stream
	PTPI_HEADER TpiHeader = Entry->TpiStream;

	Entry->HashStream = Streams[TpiHeader->HashStreamIndex];

	// Get rid of any streams we don't need anymore
	while (*Streams != NULL)
	{
		PVOID Stream = *Streams;
		if (Stream == Entry->DbiStream ||
			Stream == Entry->IpiStream ||
			Stream == Entry->TpiStream ||
			Stream == Entry->HashStream ||
			Stream == Entry->PubSymStream)
			continue;

		ImpFreeAllocation(Stream);
	}
}

NTSTATUS
PdbParseFile(
	_In_ FNV1A Name,
	_In_ PVOID ImageBase,
	_In_ PVOID Pdb
)
/*++
Routine Description:
	This function parses a PDB and stores all information necessary to extract type and symbol information in a PDB entry
--*/
{
	PPDB_ENTRY Entry = NULL;
	if (!NT_SUCCESS(PdbAllocateEntry(&Entry)))
		return STATUS_INSUFFICIENT_RESOURCES;

	Entry->NameHash = Name;
	
	// Parse the MSF headers and store important streams
	PdbParseMSF(Entry, Pdb);

	return STATUS_SUCCESS;
}
