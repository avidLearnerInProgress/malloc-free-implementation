/*
sbrk() returns address of program break
sbrk(0) -> current address of pb
sbrk(x) -> increments brk by x, resulting in allocating mem
sbrk(-x) -> decrements brk by x, resulting in allocating mem
sbrk returns (void *) -1 -> on failure 
*/

//on success, sz bytes are allocated on heap
void malloc(size_t size){
    void *block;
    block = sbrk(size);
    if(block != (void *) -1)
        return block;
    return NULL;
}

/*
free(ptr) -> 
frees memory block pointed to by ptr. 
Here ptr must have been returned by a previous call to malloc()
For freeing the mem block pointed by ptr; 
firstly we need to know the size of memory block to be freed.
Find a way to store size of allocated block somewhere..
We can only release memory that is at the end of heap since mem alloc is continous
*/
/*
From now onwards :- free != release
free - keep block marked as free
For each block of allocated memory :-
    1. size
    2. is_free?
Add header to every newly allocated mem block.
*/

struct header_t{
    size_t size;
    unsigned is_free;
};

//total_size = header_size + size; sbrk(total_size);
//to keep track of of mem allocated by malloc(), put them in LL.
//Header :-
struct header_t{
    size_t size;
    unsigned is_free;
    struct header_t* next;
};

//||header | memory block||-->||header | memory block||-->||header | memory block ||-->...
//until now we are working only on the header part..

//wrapping header in the union

typedef char ALIGN[16];

//the union guarantees that the end of the header is memory aligned
//End of header is where the mem block begins.
//Thus, memory provided to the caller by the allcoate will be aligned to 16 bytes

/*
Now, let’s wrap the entire header struct in a union along with a stub variable of size 16 bytes. This makes the header end up on a memory address aligned to 16 bytes. Recall that the size of a union is the larger size of its members. So the union guarantees that the end of the header is memory aligned. The end of the header is where the actual memory block begins and therefore the memory provided to the caller by the allocator will be aligned to 16 bytes.
*/
union header{
    struct {
        size_t size;
        unsigned is_free;
        union header *next;
    }s;
    ALIGN stub; //stores the base address of memory block that has to be allocated..!
};
typedef union header header_t;
header_t *head, *tail;

//to prevent 2 or more threads from concurrently accessing memory, lets apply locking mechanisms..
pthread_mutex_t global_malloc_lock;

//Finally malloc() :-
void malloc(size_t size){
    size_t total_sz;
    void *block;
    header_t *head_;
    if(!size) //check if requested size is 0, if yes, return NULL
        return NULL;
    pthread_mutex_lock(&global_malloc_lock); // acquire lock for valid size
    head_ = get_free_block(size); //returns free block of mem
    // traverses LL and sees if there exists block marked as free and can accomodate given size.
    //First fit approach in searching the linked list
    
    if(head_){ // sufficiently large block found.. mark as not-free
        head_->s.is_free = 0;
			// release global lock and return pointer to that block
        pthread_mutex_unlock(&global_malloc_lock);
        return (void*)(head_ + 1); // here, header ptr will refer to header part of block of mem..
    } // (head_ + 1) --> hiding header from the user and returning the memory directly..
    //hiding header from the user
    
    /*
    if sufficiently large block not found;
        extend the heap by calling sbrk()
    */
	
    total_sz = sizeof(head_) + size; // extending the dynamic memory
    block = sbrk(total_sz); //increment the program break by total size
    if(block == (void*) -1){ 
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;	
    }//if unable to extend the page break; return NULL
	
	//able to extend..
    //make space for header
    head_ = block;
    head_->s.size = size;
    head_->s.is_free = 0;
    head_->s.next = NULL;
    if(!head) //if its the first block of the memory
        head = head_; 
    if(tail) //else append to the existing list of already existing list
        tail->s.next = head_;
    tail = head_;
    pthread_mutex_unlock(&global_malloc_lock);
    return (void*)(head_ + 1);
}

header_t *get_free_block(size_t sz){
    head_ *curr = head;
    while(curr){
        if(curr->s.is_free && curr->s.size >= sz){
            return curr;            
        }
        curr = curr->next;
    }
    return NULL;
}


/*----------Free implementation in C----------*/
/*
Now, we will look at what free() should do. free() has to first deterimine if the block-to-be-freed is at the end of the heap. If it is, we can release it to the OS. Otherwise, all we do is mark it ‘free’, hoping to reuse it later.
*/
void free(void *block)
{
	header_t *header, *tmp;
	void *programbreak;

	if (!block)
		return;
	pthread_mutex_lock(&global_malloc_lock);
	header = (header_t*)block - 1;

	programbreak = sbrk(0);
	if ((char*)block + header->s.size == programbreak) {
		if (head == tail) {
			head = tail = NULL;
		} else {
			tmp = head;
			while (tmp) {
				if(tmp->s.next == tail) {
					tmp->s.next = NULL;
					tail = tmp;
				}
				tmp = tmp->s.next;
			}
		}
		sbrk(0 - sizeof(header_t) - header->s.size);
		pthread_mutex_unlock(&global_malloc_lock);
		return;
	}
	header->s.is_free = 1;
	pthread_mutex_unlock(&global_malloc_lock);
}

/*
Here, first we get the header of the block we want to free. All we need to do is get a pointer that is behind the block by a distance equalling the size of the header. So, we cast block to a header pointer type and move it behind by 1 unit.
header = (header_t*)block - 1;

sbrk(0) gives the current value of program break. To check if the block to be freed is at the end of the heap, we first find the end of the current block. The end can be computed as (char*)block + header->s.size. This is then compared with the program break.

If it is in fact at the end, then we could shrink the size of the heap and release memory to OS. We first reset our head and tail pointers to reflect the loss of the last block. Then the amount of memory to be released is calculated. This the sum of sizes of the header and the acutal block: sizeof(header_t) + header->s.size. To release this much amount of memory, we call sbrk() with the negative of this value.

In the case the block is not the last one in the linked list, we simply set the is_free field of its header. This is the field checked by get_free_block() before actually calling sbrk() on a malloc().
*/