#define ASSOOFS_MAGIC 0x20170509
#define ASSOOFS_DEFAULT_BLOCK_SIZE 4096
#define ASSOOFS_FILENAME_MAXLEN 255
#define ASSOOFS_START_INO 10

/* Reserved inodes for super block, inodestore and datablock */
#define ASSOOFS_RESERVED_INODES 3 

/* The disk block where super block is stored */
const int ASSOOFS_SUPERBLOCK_BLOCK_NUMBER = 0;

/* The disk block where the inodes are stored */
const int ASSOOFS_INODESTORE_BLOCK_NUMBER = 1;

/* The disk block where the name+inode_number pairs of the contents of the root directory are stored */
const int ASSOOFS_ROOTDIR_DATABLOCK_NUMBER = 2;

#define ASSOOFS_LAST_RESERVED_BLOCK ASSOOFS_ROOTDIR_DATABLOCK_NUMBER
#define ASSOOFS_LAST_RESERVED_INODE ASSOOFS_INODESTORE_BLOCK_NUMBER

/* Hard-coded inode number for the root directory */
const int ASSOOFS_ROOTDIR_INODE_NUMBER = 1;

const int ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED = 64;

struct assoofs_super_block_info {
    uint64_t version;
    uint64_t magic;
    uint64_t block_size;    
    uint64_t inodes_count; /* FIXME: This should be moved to the inode store and not part of the sb */
    uint64_t free_blocks;
    char padding[4056];
};

/* The name+inode_number pair for each file in a directory. This gets stored as the data for a directory */
struct assoofs_dir_record_entry {
    char filename[ASSOOFS_FILENAME_MAXLEN];
    uint64_t inode_no;
};

struct assoofs_inode_info {
    mode_t mode;
    uint64_t inode_no;
    uint64_t data_block_number;
    union {
        uint64_t file_size;
        uint64_t dir_children_count;
    };
};