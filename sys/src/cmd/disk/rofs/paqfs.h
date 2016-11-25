typedef struct PaqHeader PaqHeader;
typedef struct PaqBlock PaqBlock;
typedef struct PaqTrailer PaqTrailer;
typedef struct PaqDir PaqDir;

enum {
	HeaderMagic = 0x529ab12b,
	HeaderSize = 44,
	BigHeaderMagic = 0x25a9,
	BlockMagic = 0x198a1cbf,
	BlockSize = 12,
	BigBlockMagic = 0x91a8,
	TrailerMagic = 0x6b46e688,
	TrailerSize = 28,
	Version = 1,
	MaxBlockSize = 512*1024,
	MinBlockSize = 512,
	MinDirSize = 28,
};

/* block types */
enum {
	DirBlock,
	DataBlock,
	PointerBlock,
};

/* encodings */
enum {
	NoEnc,
	DeflateEnc,
};

struct PaqHeader
{
	uint32_t	magic;
	uint16_t	version;
	uint32_t	blocksize;
	uint32_t	time;
	char	label[32];
};

struct PaqBlock
{
	uint32_t	magic;
	uint32_t	size;		/* data size - always <= blocksize */
	uint8_t	type;
	uint8_t	encoding;
	uint32_t	adler32;	/* applied to unencoded data */
};

struct PaqTrailer
{
	uint32_t	magic;
	uint32_t	root;
	uint8_t	sha1[20];
};

struct PaqDir
{
	uint32_t	qid;
	uint32_t	mode;
	uint32_t	mtime;
	uint32_t	length;
	uint32_t	offset;		/* to pointer block */
	char 	*name;
	char	*uid;
	char	*gid;
};
