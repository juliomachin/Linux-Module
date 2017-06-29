#include <linux/module.h>       /* Needed by all modules */
#include <linux/kernel.h>       /* Needed for KERN_INFO  */
#include <linux/init.h>         /* Needed for the macros */
#include <linux/fs.h>           /* libfs stuff           */
#include <asm/uaccess.h>        /* copy_to_user          */
#include <linux/buffer_head.h>  /* buffer_head           */
#include <linux/slab.h>         /* kmem_cache            */
#include "assoofs.h"



struct assoofs_inode_info * assoofs_get_inode_info(struct super_block *sb, uint64_t ino);
static struct inode *assoofs_get_inode(struct super_block *sb, uint64_t ino);
static int assoofs_create_fs_object(struct inode *dir, struct dentry *dentry, umode_t mode);
void assoofs_sb_sync(struct super_block *sb);
void assoofs_inode_add(struct super_block *sb, struct assoofs_inode_info  *inode);
int assoofs_inode_save(struct super_block *sb, struct assoofs_inode_info *sfs_inode);
ssize_t assoofs_read ( struct file *filp , char __user *buf , size_t len , loff_t * ppos );
ssize_t assoofs_write ( struct file *filp , const char __user *buf , size_t len , loff_t * ppos );
static int assoofs_iterate ( struct file *filp , struct dir_context * ctx );
struct dentry * assoofs_lookup ( struct inode * parent_inode , struct dentry * child_dentry , unsigned int flags );
static int assoofs_create ( struct inode *dir , struct dentry * dentry , umode_t mode , bool excl );
static int assoofs_mkdir ( struct inode *dir , struct dentry * dentry , umode_t mode );
int assoofs_fill_super(struct super_block *sb, void *data, int silent);

void assoofs_sb_sync(struct super_block *sb)
{
	struct buffer_head *bh;
	struct assoofs_super_block *sb_i = sb->s_fs_info;

	printk(KERN_INFO "Entro al sync");
	bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);

	bh->b_data = (char *)sb_i;
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

void assoofs_inode_add(struct super_block *sb, struct assoofs_inode_info  *inode)
{
	struct assoofs_super_block_info *sb_disk;
	struct buffer_head *bh;
	struct assoofs_inode_info *inode_iterator;

	sb_disk = sb->s_fs_info;

	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);

	inode_iterator = (struct assoofs_inode_info *)bh->b_data;

	inode_iterator += sb_disk->inodes_count;

	memcpy(inode_iterator, inode, sizeof(struct assoofs_inode_info));
	sb_disk->inodes_count++;

	mark_buffer_dirty(bh);
	assoofs_sb_sync(sb);	
	brelse(bh);

}


int assoofs_inode_save(struct super_block *sb, struct assoofs_inode_info *i_inode)
{
	struct assoofs_inode_info *inode_iterator;
	struct buffer_head *bh;

	bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
	inode_iterator = assoofs_get_inode_info(sb,i_inode->inode_no);

	if (inode_iterator) {
		memcpy(inode_iterator, i_inode, sizeof(*inode_iterator));
		printk(KERN_INFO "The inode updated\n");
		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	} 
	brelse(bh);
	return 0;
}

ssize_t assoofs_read ( struct file *filp , char __user *buf , size_t len , loff_t * ppos ){
	struct inode *inode;
	struct buffer_head *bh;
	struct super_block *sb;
	struct assoofs_inode_info *i_inode;
	char *buffer;
	size_t nbytes;

	printk(KERN_INFO "READ");

	sb = filp->f_path.dentry->d_inode->i_sb;
	inode = filp->f_path.dentry->d_inode;
	i_inode = inode->i_private;
	if (*ppos >= i_inode->file_size) {
		return 0;
	}
	bh = sb_bread(sb, i_inode->data_block_number);	
	if (!bh) {
		printk(KERN_ERR "Reading the block number [%llu] failed.",
		       i_inode->data_block_number);
		return 0;
	}
	buffer = (char *)bh->b_data;
	nbytes = min((size_t) i_inode->file_size, len);

	if (copy_to_user(buf, buffer, nbytes)) {
		brelse(bh);
		printk(KERN_ERR
		       "Error copying file contents to the userspace buffer\n");
		return 0;
	}

	brelse(bh);

	*ppos += nbytes;

	return nbytes;

}
ssize_t assoofs_write ( struct file *filp , const char __user *buf , size_t len , loff_t * ppos ){


	struct inode *inode;
	struct buffer_head *bh;
	struct super_block *sb;
	struct assoofs_inode_info *i_inode;
	char *buffer;

	printk(KERN_INFO "WRITE");

	sb = filp->f_path.dentry->d_inode->i_sb;
	inode = filp->f_path.dentry->d_inode;
	i_inode = inode->i_private;


	bh = sb_bread(sb, i_inode->data_block_number);	

	if (!bh) {
		printk(KERN_ERR "Reading the block number [%llu] failed.",
		       i_inode->data_block_number);
		return 0;
	}

	buffer = (char *)bh->b_data;
	buffer += *ppos;
	
	if (copy_from_user(buffer, buf, len)) {
		brelse(bh);
		printk(KERN_ERR "Error en write\n");
		return 0;
	}

	*ppos += len;

	i_inode->file_size = *ppos;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	assoofs_inode_save(sb, i_inode);
	
	return len;


}
static int assoofs_iterate ( struct file *filp , struct dir_context * ctx ){
	struct inode *inode;
	struct buffer_head  *bh;
	struct assoofs_dir_record_entry *record;
	struct assoofs_inode_info * i_info;
	int i;
	printk(KERN_INFO "ITERATE");
	if(ctx->pos)	
		return 0;

	inode = filp->f_path.dentry->d_inode; //filp es el descriptor de ficheros. Esto cambia con la version de kernel, con las antiguas falla.
	i_info = inode->i_private;

	if (!S_ISDIR(i_info->mode)) {
		printk(KERN_ERR
		       "inode [%llu][%lu] for fs object [] not a directory\n",
		       i_info->inode_no, inode->i_ino);
		return 0;
	}
	bh = sb_bread(inode->i_sb, i_info->data_block_number);
	record = (struct assoofs_dir_record_entry *) bh-> b_data;
	//recorrer los dir
	for(i = 0;i<i_info->dir_children_count;i++){
		dir_emit(ctx,record->filename,255,record->inode_no, DT_UNKNOWN);
		ctx-> pos += sizeof(struct assoofs_dir_record_entry);
		record++;
	}
	brelse(bh);
	return 0;
}


//Crea arbol de directorios

struct dentry * assoofs_lookup ( struct inode * parent_inode , struct dentry * child_dentry , unsigned int flags ){
	struct assoofs_inode_info * i_parent;
	struct super_block *sb;
	struct assoofs_dir_record_entry *record;
	struct buffer_head *bh;
	struct inode *in ;
	struct assoofs_inode_info * i_inf;
	int i;

	printk(KERN_INFO "LOOKUP");

	i_parent = parent_inode-> i_private;
	sb = parent_inode->i_sb;
	bh = sb_bread(sb,i_parent->data_block_number);
	record = (struct assoofs_dir_record_entry *)bh->b_data;


	for(i=0;i<i_parent->dir_children_count;i++){
 		if (!strcmp(record->filename, child_dentry->d_name.name)){
		
			printk(KERN_INFO "Entro");
			in = assoofs_get_inode(sb, record->inode_no);
			i_inf = in->i_private;
			inode_init_owner(in,parent_inode,i_inf->mode);
			d_add(child_dentry, in);
			return NULL;
		}
		
		record++;
	}
	return 0;
	
}
static int assoofs_create ( struct inode *dir , struct dentry * dentry , umode_t mode , bool excl ){
	printk(KERN_INFO "CREATE");
	return assoofs_create_fs_object(dir, dentry, mode);
}	
static int assoofs_mkdir ( struct inode *dir , struct dentry * dentry , umode_t mode ){
	printk(KERN_INFO "CREATE MKDIR");
	return assoofs_create_fs_object(dir, dentry, S_IFDIR | mode);
}




static  struct inode_operations assoofs_inode_ops = {
	.create = assoofs_create,
	.lookup = assoofs_lookup,
	.mkdir = assoofs_mkdir,
};
const struct file_operations assoofs_file_operations = {

	.read = assoofs_read,
	.write = assoofs_write,
};

const struct file_operations assoofs_dir_operations = {
	.owner = THIS_MODULE,
	.iterate = assoofs_iterate,

};

static const struct super_operations assoofs_sops = {
	.drop_inode = generic_delete_inode,
};


static int assoofs_create_fs_object(struct inode *dir, struct dentry *dentry,
				     umode_t mode)
{
	struct inode *inode;
	struct assoofs_inode_info *i_info;
	struct super_block *sb;
	struct assoofs_inode_info *parent_dir_inode;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *dir_contents_datablock;
	struct assoofs_super_block_info *sb_disk;
	uint64_t count;
	int ret;
	int i;

	sb = dir->i_sb;
	sb_disk = sb->s_fs_info;
	
	//numero de inodos
	count = sb_disk->inodes_count;
	
	inode = new_inode(sb);
	inode->i_sb = sb;
	inode->i_op = &assoofs_inode_ops;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	
	//numero de inodo
	inode->i_ino = (count + ASSOOFS_START_INO - ASSOOFS_RESERVED_INODES + 1);


	i_info = assoofs_get_inode_info(sb, inode->i_ino);
		
	i_info->inode_no = inode->i_ino;


	inode->i_private = i_info;

	i_info->mode = mode;

	if (S_ISDIR(mode)) {
		printk(KERN_INFO "New directory creation request\n");
		i_info->dir_children_count = 0;
		inode->i_fop = &assoofs_dir_operations;
	} else if (S_ISREG(mode)) {
		printk(KERN_INFO "New file creation request\n");
		i_info->file_size = 0;
		inode->i_fop = &assoofs_file_operations;
	}

	//Conseguir un nuevo bloque
	for (i = 4; i < ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED; i++) {
		if (sb_disk->free_blocks & (1 << i)) {
			break;
		}
	}
	i_info->data_block_number = i;
	sb_disk->free_blocks &= ~(1 << i);

	assoofs_sb_sync(sb);
	assoofs_inode_add(sb, i_info);

	//inodo padre
	parent_dir_inode = dir->i_private;
	bh = sb_bread(sb, parent_dir_inode->data_block_number);

	dir_contents_datablock = (struct assoofs_dir_record_entry *)bh->b_data;
	dir_contents_datablock += parent_dir_inode->dir_children_count;
	dir_contents_datablock->inode_no = i_info->inode_no;
	strcpy(dir_contents_datablock->filename, dentry->d_name.name);

	assoofs_inode_save(sb, parent_dir_inode);
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);


	parent_dir_inode->dir_children_count++;
	ret = assoofs_inode_save(sb, parent_dir_inode);

	inode_init_owner(inode, dir, mode);
	d_add(dentry, inode);

	return 0;
}



//para informacion
struct assoofs_inode_info * assoofs_get_inode_info(struct super_block *sb, uint64_t ino){

	struct assoofs_inode_info *a_inode;
	struct buffer_head  *bh;
	struct assoofs_super_block_info *sb_disk;
	int i;
	bh = sb_bread(sb, 1);
	a_inode = (struct assoofs_inode_info *) bh-> b_data;
	sb_disk = sb->s_fs_info;
	for(i = 0;i<sb_disk->inodes_count;i++){	
		if(a_inode->inode_no == ino){
			break;
		}
		a_inode++;//puntero a una variable de assoofs_inode_info
	}
	brelse(bh);//liberar memoria dinamica
	//podemos comprobar errores
	return a_inode;	
}

//para inodo
static struct inode *assoofs_get_inode(struct super_block *sb, uint64_t ino){
	struct assoofs_inode_info * a_inode;
	struct inode *i;	
	a_inode = assoofs_get_inode_info(sb, ino);
	i = new_inode(sb);
	i->i_ino = ino;
	i->i_sb = sb;
	i->i_op = &assoofs_inode_ops;
	if(S_ISDIR(a_inode->mode))
		i->i_fop = &assoofs_dir_operations;
	else
		i->i_fop = &assoofs_file_operations;
	i->i_atime = i->i_mtime = i->i_ctime = CURRENT_TIME;
	i->i_private = a_inode;

	return i;
}

int assoofs_fill_super(struct super_block *sb, void *data, int silent)
{
//Cosas que voy a necesitar(?)
	struct inode *root_inode;
	struct buffer_head *bh;
	struct assoofs_super_block_info *sb_disk;


//Leer informacion de bloques
	bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);


	//Utilizar assofs_super_block_info¿¿??? 
	sb_disk = (struct assoofs_super_block_info *)bh->b_data;	
	printk(KERN_INFO "The magic number obtained in disk is: [%llu]\n",
	       sb_disk->magic);

//Comprobar parametros de superbloque

	if (sb_disk->magic != ASSOOFS_MAGIC) {
		printk(KERN_ERR "The filesystem that you try to mount is not of type assoofs. Magicnumber mismatch.");
		return -1;
	}

	if (sb_disk->block_size != ASSOOFS_DEFAULT_BLOCK_SIZE) {
		printk(KERN_ERR "assoofs seem to be formatted using a non-standard block size.");
		return -1;
	}


	printk(KERN_INFO"assoofs filesystem of version [%llu] formatted with a block size of [%llu] detected in the device.\n", sb_disk->version, sb_disk->block_size);



//Escribir informacion leida del dispositivo de bloques en el super bloque
	sb->s_magic = ASSOOFS_MAGIC;
	sb->s_fs_info = sb_disk;
	sb->s_maxbytes = ASSOOFS_DEFAULT_BLOCK_SIZE;
	sb->s_op = &assoofs_sops;
	
//Crear inodo raiz
	//Creo el inodo	

	root_inode = new_inode(sb);

	root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
	inode_init_owner(root_inode, NULL, S_IFDIR);
	//informacion


	root_inode->i_sb = sb;

	root_inode->i_op = &assoofs_inode_ops;
	root_inode->i_fop = &assoofs_dir_operations;
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = CURRENT_TIME;

	root_inode->i_private = assoofs_get_inode_info(sb, 1);



	sb->s_root = d_make_root(root_inode);
	brelse(bh);
	printk(KERN_ERR "Fin fill_super");
	return 0;

}

static struct dentry *assoofs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *data)
{

	struct dentry * ret;
	ret = mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);

	printk(KERN_INFO "Mount\n");
	return ret;
}

struct file_system_type assoofs_type = {
	.owner = THIS_MODULE,
	.name = "assoofs",
	.mount = assoofs_mount,
	.kill_sb = kill_litter_super,
	
};

static int __init assoofs_init(void)
{
	int ret;
	ret = register_filesystem(&assoofs_type);
	if (ret == 0)
		printk(KERN_INFO "Sucessfully registered assoofs\n");
	else
		printk(KERN_ERR "Failed to register assoofs. Error:[%d]", ret);

	return ret;
}

static void __exit assoofs_exit(void)
{
    	int ret;

	ret = unregister_filesystem(&assoofs_type);


	if (ret == 0)
		printk(KERN_INFO "Sucessfully unregistered assoofs\n");
	else
		printk(KERN_ERR "Failed to unregister assoofs. Error:[%d]",
		       ret);
}

module_init(assoofs_init);
module_exit(assoofs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julio Machin Ruiz");