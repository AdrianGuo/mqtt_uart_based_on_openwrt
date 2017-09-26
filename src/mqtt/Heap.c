/*******************************************************************************
 * Copyright (c) 2009, 2014 IBM Corp.
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * and Eclipse Distribution License v1.0 which accompany this distribution. 
 *
 * The Eclipse Public License is available at 
 *    http://www.eclipse.org/legal/epl-v10.html
 * and the Eclipse Distribution License is available at 
 *   http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *    Ian Craggs - use tree data structure instead of list
 *    Ian Craggs - change roundup to Heap_roundup to avoid macro name clash on MacOSX
 *******************************************************************************/
//-堆:一般由程序员分配释放
//-管理堆的函数有消除内存泄漏的目的
/**
 * @file
 * \brief functions to manage the heap with the goal of eliminating memory leaks
 *
 * For any module to use these functions transparently, simply include the Heap.h
 * header file.  Malloc and free will be redefined, but will behave in exactly the same
 * way as normal, so no recoding is necessary.
 *
 * */
//-堆，队列优先,先进先出（FIFO—first in first out)。栈，先进后出(FILO—First-In/Last-Out)。
#include "Tree.h"
#include "Log.h"
#include "StackTrace.h"
#include "Thread.h"
char* Broker_recordFFDC(char* symptoms);

#include <memory.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#include "Heap.h"

#undef malloc
#undef realloc
#undef free

#if defined(WIN32) || defined(WIN64)
mutex_type heap_mutex;
#else
static pthread_mutex_t heap_mutex_store = PTHREAD_MUTEX_INITIALIZER;	//-静态初始化互斥锁
static mutex_type heap_mutex = &heap_mutex_store;
#endif

static heap_info state = {0, 0}; /**< global heap state information */
static int eyecatcher = 0x88888888;		//-识别序列

/**
 * Each item on the heap is recorded with this structure.
 */
typedef struct
{
	char* file;		/**< the name of the source file where the storage was allocated */
	int line;		/**< the line no in the source file where it was allocated */
	void* ptr;		/**< pointer to the allocated storage */
	int size;       /**< size of the allocated storage */
} storageElement;	//-在堆中的每个成员用这个结构体记录属性.

static Tree heap;	/**< Tree that holds the allocation records */
static char* errmsg = "Memory allocation error";

/**
 * Round allocation size up to a multiple of the size of an int.  Apart from possibly reducing fragmentation,
 * on the old v3 gcc compilers I was hitting some weird behaviour, which might have been errors in
 * sizeof() used on structures and related to packing.  In any case, this fixes that too.
 * @param size the size actually needed
 * @return the rounded up size
 */
int Heap_roundup(int size)	//-根据实际需要的内存尺寸,分配一个整数内存尺寸保证地址对齐,否则也是浪费,也是一个内存泄漏
{
	static int multsize = 4*sizeof(int);

	if (size % multsize != 0)
		size += multsize - (size % multsize);
	return size;
}


/**
 * List callback function for comparing storage elements
 * @param a pointer to the current content in the tree (storageElement*)
 * @param b pointer to the memory to free
 * @return boolean indicating whether a and b are equal
 */
int ptrCompare(void* a, void* b, int value)
{
	a = ((storageElement*)a)->ptr;
	if (value)
		b = ((storageElement*)b)->ptr;

	return (a > b) ? -1 : (a == b) ? 0 : 1;
}


void Heap_check(char* string, void* ptr)
{
	return;
	/*Node* curnode = NULL;
	storageElement* prev, *s = NULL;

	printf("Heap_check start %p\n", ptr);
	while ((curnode = TreeNextElement(&heap, curnode)) != NULL)
	{
		prev = s;
		s = (storageElement*)(curnode->content);

		if (prev)
		{
		if (ptrCompare(s, prev, 1) != -1)
		{
			printf("%s: heap order error %d %p %p\n", string, ptrCompare(s, prev, 1), prev->ptr, s->ptr);
			exit(99);
		}
		else
			printf("%s: heap order good %d %p %p\n", string, ptrCompare(s, prev, 1), prev->ptr, s->ptr);
		}
	}*/
}


/**
 * Allocates a block of memory.  A direct replacement for malloc, but keeps track of items
 * allocated in a list, so that free can check that a item is being freed correctly and that
 * we can check that all memory is freed at shutdown.
 * @param file use the __FILE__ macro to indicate which file this item was allocated in
 * @param line use the __LINE__ macro to indicate which line this item was allocated at
 * @param size the size of the item to be allocated
 * @return pointer to the allocated item, or NULL if there was an error
 */
void* mymalloc(char* file, int line, size_t size)	//-分配一个空白的空间. 用于直接替代malloc函数的.但是保持被分配的项目踪迹在一个列表中
{																									//-所以释放时能够检查一个正在被正确释放的项目并能够检查所有的内容是否被释放在关闭的时候
	storageElement* s = NULL;
	int space = sizeof(storageElement);
	int filenamelen = strlen(file)+1;

	Thread_lock_mutex(heap_mutex);
	size = Heap_roundup(size);
	if ((s = malloc(sizeof(storageElement))) == NULL)	//-从这里应该是可以看出,不是简单的取消malloc函数功能,而是增加了跟踪
	{
		Log(LOG_ERROR, 13, errmsg);
		return NULL;
	}
	s->size = size; /* size without eyecatchers */
	if ((s->file = malloc(filenamelen)) == NULL)
	{
		Log(LOG_ERROR, 13, errmsg);
		free(s);
		return NULL;
	}
	space += filenamelen;
	strcpy(s->file, file);	//-记录了文件名字,并且用这个指针指到了对应的位置
	s->line = line;	//-又一次记录了一个属性值
	/* Add space for eyecatcher at each end */
	if ((s->ptr = malloc(size + 2*sizeof(int))) == NULL)	//-实际分配空间还是用malloc函数实现的,这里的分配仅仅是增加了一个属性
	{
		Log(LOG_ERROR, 13, errmsg);
		free(s->file);
		free(s);
		return NULL;
	}
	space += size + 2*sizeof(int);	//-记录了整个空间的大小,包括属性结构
	*(int*)(s->ptr) = eyecatcher; /* start eyecatcher */
	*(int*)(((char*)(s->ptr)) + (sizeof(int) + size)) = eyecatcher; /* end eyecatcher */
	Log(TRACE_MAX, -1, "Allocating %d bytes in heap at file %s line %d ptr %p\n", size, file, line, s->ptr);
	TreeAdd(&heap, s, space);	//-堆是用一个树结构来记录的,现在在这个结构中增加一个成员.S记录了整个分配空间的属性,space记录了大小
	state.current_size += size;
	if (state.current_size > state.max_size)
		state.max_size = state.current_size;	//-这样语句的功能是记录出现过的最大值
	Thread_unlock_mutex(heap_mutex);		
	return ((int*)(s->ptr)) + 1;	/* skip start eyecatcher */
}


void checkEyecatchers(char* file, int line, void* p, int size)	//-检查识别序列
{
	int *sp = (int*)p;
	char *cp = (char*)p;
	int us;
	static char* msg = "Invalid %s eyecatcher %d in heap item at file %s line %d";

	if ((us = *--sp) != eyecatcher)
		Log(LOG_ERROR, 13, msg, "start", us, file, line);

	cp += size;
	if ((us = *(int*)cp) != eyecatcher)
		Log(LOG_ERROR, 13, msg, "end", us, file, line);
}


/**
 * Remove an item from the recorded heap without actually freeing it.
 * Use sparingly!
 * @param file use the __FILE__ macro to indicate which file this item was allocated in
 * @param line use the __LINE__ macro to indicate which line this item was allocated at
 * @param p pointer to the item to be removed
 */
int Internal_heap_unlink(char* file, int line, void* p)	//-移除一个项目从堆的记录中,但是没有实际释放他.慎重的使用.
{
	Node* e = NULL;
	int rc = 0;

	e = TreeFind(&heap, ((int*)p)-1);
	if (e == NULL)
		Log(LOG_ERROR, 13, "Failed to remove heap item at file %s line %d", file, line);
	else
	{
		storageElement* s = (storageElement*)(e->content);
		Log(TRACE_MAX, -1, "Freeing %d bytes in heap at file %s line %d, heap use now %d bytes\n",
											 s->size, file, line, state.current_size);
		checkEyecatchers(file, line, p, s->size);
		//free(s->ptr);
		free(s->file);
		state.current_size -= s->size;
		TreeRemoveNodeIndex(&heap, e, 0);
		free(s);
		rc = 1;
	}
	return rc;
}


/**
 * Frees a block of memory.  A direct replacement for free, but checks that a item is in
 * the allocates list first.
 * @param file use the __FILE__ macro to indicate which file this item was allocated in
 * @param line use the __LINE__ macro to indicate which line this item was allocated at
 * @param p pointer to the item to be freed
 */
void myfree(char* file, int line, void* p)	//-释放一个空白的内存.直接置换,但是首先检查项目是否在分配列表内
{
	Thread_lock_mutex(heap_mutex);
	if (Internal_heap_unlink(file, line, p))
		free(((int*)p)-1);
	Thread_unlock_mutex(heap_mutex);
}


/**
 * Remove an item from the recorded heap without actually freeing it.
 * Use sparingly!
 * @param file use the __FILE__ macro to indicate which file this item was allocated in
 * @param line use the __LINE__ macro to indicate which line this item was allocated at
 * @param p pointer to the item to be removed
 */
void Heap_unlink(char* file, int line, void* p)	//-移除一个项目从堆的记录中,但是没有实际释放他.慎重的使用.
{
	Thread_lock_mutex(heap_mutex);
	Internal_heap_unlink(file, line, p);
	Thread_unlock_mutex(heap_mutex);
}


/**
 * Reallocates a block of memory.  A direct replacement for realloc, but keeps track of items
 * allocated in a list, so that free can check that a item is being freed correctly and that
 * we can check that all memory is freed at shutdown.
 * We have to remove the item from the tree, as the memory is in order and so it needs to
 * be reinserted in the correct place.
 * @param file use the __FILE__ macro to indicate which file this item was reallocated in
 * @param line use the __LINE__ macro to indicate which line this item was reallocated at
 * @param p pointer to the item to be reallocated
 * @param size the new size of the item
 * @return pointer to the allocated item, or NULL if there was an error
 */
void *myrealloc(char* file, int line, void* p, size_t size)	//-再分配一个空白内存.直接替换realloc.但是保持项目在列表中分配的踪迹,所以释放
{																														//-能检查一个项目是否正被正确释放并且能够检查所有内容是否被释放在关闭的时候
	void* rc = NULL;																					//-我们不得不从树里面移除项目,由于内存是按次序的,所以需要再次插入到正确位置
	storageElement* s = NULL;
	
	Thread_lock_mutex(heap_mutex);
	s = TreeRemoveKey(&heap, ((int*)p)-1);
	if (s == NULL)
		Log(LOG_ERROR, 13, "Failed to reallocate heap item at file %s line %d", file, line);
	else
	{
		int space = sizeof(storageElement);
		int filenamelen = strlen(file)+1;

		checkEyecatchers(file, line, p, s->size);
		size = Heap_roundup(size);
		state.current_size += size - s->size;
		if (state.current_size > state.max_size)
			state.max_size = state.current_size;
		if ((s->ptr = realloc(s->ptr, size + 2*sizeof(int))) == NULL)
		{
			Log(LOG_ERROR, 13, errmsg);
			return NULL;
		}
		space += size + 2*sizeof(int) - s->size;
		*(int*)(s->ptr) = eyecatcher; /* start eyecatcher */
		*(int*)(((char*)(s->ptr)) + (sizeof(int) + size)) = eyecatcher; /* end eyecatcher */
		s->size = size;
		space -= strlen(s->file);
		s->file = realloc(s->file, filenamelen);
		space += filenamelen;
		strcpy(s->file, file);
		s->line = line;
		rc = s->ptr;
		TreeAdd(&heap, s, space);
	}
	Thread_unlock_mutex(heap_mutex);
	return (rc == NULL) ? NULL : ((int*)(rc)) + 1;	/* skip start eyecatcher */
}


/**
 * Utility to find an item in the heap.  Lets you know if the heap already contains
 * the memory location in question.
 * @param p pointer to a memory location
 * @return pointer to the storage element if found, or NULL
 */
void* Heap_findItem(void* p)	//-通用的在堆里面查找一个项目.让你知道堆是否已经包含了在讨论中的内存位置.
{
	Node* e = NULL;

	Thread_lock_mutex(heap_mutex);
	e = TreeFind(&heap, ((int*)p)-1);
	Thread_unlock_mutex(heap_mutex);
	return (e == NULL) ? NULL : e->content;
}


/**
 * Scans the heap and reports any items currently allocated.
 * To be used at shutdown if any heap items have not been freed.
 */
void HeapScan(int log_level)	//-浏览堆并且报告一些项目当前的分配值.被用于在关闭的时候看堆的一些项目是否没有已经被释放.
{
	Node* current = NULL;
	
	Thread_lock_mutex(heap_mutex);
	Log(log_level, -1, "Heap scan start, total %d bytes", state.current_size);
	while ((current = TreeNextElement(&heap, current)) != NULL)
	{
		storageElement* s = (storageElement*)(current->content);
		Log(log_level, -1, "Heap element size %d, line %d, file %s, ptr %p", s->size, s->line, s->file, s->ptr);
		Log(log_level, -1, "  Content %*.s", (10 > current->size) ? s->size : 10, (char*)(((int*)s->ptr) + 1));
	}
	Log(log_level, -1, "Heap scan end");
	Thread_unlock_mutex(heap_mutex);
}


/**
 * Heap initialization.
 */
int Heap_initialize()	//-仅仅进行堆栈初始化,这个是自己建立的软件堆栈?
{
	TreeInitializeNoMalloc(&heap, ptrCompare);	//-建立了一个堆的起点,在程序中以全局变量形式存在的
	heap.heap_tracking = 0; /* no recursive heap tracking! */	//-没有递归堆跟踪
	return 0;
}


/**
 * Heap termination.
 */
void Heap_terminate()	//-堆终止
{
	Log(TRACE_MIN, -1, "Maximum heap use was %d bytes", state.max_size);	//-堆的最大字节数据
	if (state.current_size > 20) /* One log list is freed after this function is called */
	{
		Log(LOG_ERROR, -1, "Some memory not freed at shutdown, possible memory leak");	//-在关机的时候一些内存没有被释放,可能内存泄露了
		HeapScan(LOG_ERROR);
	}
}


/**
 * Access to heap state
 * @return pointer to the heap state structure
 */
heap_info* Heap_get_info()	//-指向堆的状态
{
	return &state;
}


/**
 * Dump a string from the heap so that it can be displayed conveniently
 * @param file file handle to dump the heap contents to
 * @param str the string to dump, could be NULL
 */
int HeapDumpString(FILE* file, char* str)	//-转存来自堆的一个字符串以便它能够被方便的显示
{
	int rc = 0;
	int len = str ? strlen(str) + 1 : 0; /* include the trailing null */

	if (fwrite(&(str), sizeof(char*), 1, file) != 1)	//-向文件写入一个数据块
		rc = -1;
	else if (fwrite(&(len), sizeof(int), 1 ,file) != 1)
		rc = -1;
	else if (len > 0 && fwrite(str, len, 1, file) != 1)
		rc = -1;
	return rc;
}


/**
 * Dump the state of the heap
 * @param file file handle to dump the heap contents to
 */
int HeapDump(FILE* file)	//-转存堆的状态
{
	int rc = 0;
	Node* current = NULL;

	while (rc == 0 && (current = TreeNextElement(&heap, current)))
	{
		storageElement* s = (storageElement*)(current->content);

		if (fwrite(&(s->ptr), sizeof(s->ptr), 1, file) != 1)
			rc = -1;
		else if (fwrite(&(current->size), sizeof(current->size), 1, file) != 1)
			rc = -1;
		else if (fwrite(s->ptr, current->size, 1, file) != 1)
			rc = -1;
	}
	return rc;
}


#if defined(HEAP_UNIT_TESTS)

void Log(int log_level, int msgno, char* format, ...)
{
	printf("Log %s", format);
}

char* Broker_recordFFDC(char* symptoms)
{
	printf("recordFFDC");
	return "";
}

#define malloc(x) mymalloc(__FILE__, __LINE__, x)
#define realloc(a, b) myrealloc(__FILE__, __LINE__, a, b)
#define free(x) myfree(__FILE__, __LINE__, x)

int main(int argc, char *argv[])
{
	char* h = NULL;
	Heap_initialize();

	h = malloc(12);
	free(h);
	printf("freed h\n");

	h = malloc(12);
	h = realloc(h, 14);
	h = realloc(h, 25);
	h = realloc(h, 255);
	h = realloc(h, 2225);
	h = realloc(h, 22225);
    printf("freeing h\n");
	free(h);
	Heap_terminate();
	printf("Finishing\n");
	return 0;
}

#endif
