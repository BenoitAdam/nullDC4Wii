#pragma once
#include "types.h"
#include <stdlib.h>
#include "string.h"
#include <vector>

#define PAGE_SIZE 4096
#define PAGE_MASK (PAGE_SIZE-1)
#include "plugins/plugin_header.h"
#include "os_base.h"


extern u32 Array_T_id_count;
char* GetNullDCSoruceFileName(const char* full);
void GetPathFromFileName(char* full);
void GetFileNameFromPath(char* path,char* outp);
u32 fastrand();
//comonly used classes across the project
//Simple Array class for helping me out ;P
template<class T>
class Array
{
public:
	T* data;
	u32 Size;
	u32 id;

	Array(T* Source,u32 ellements)
	{
		//initialise array
		Array_T_id_count+=1;
		id=Array_T_id_count;
		data=Source;
		Size=ellements;
	}

	Array(u32 ellements)
	{
		//initialise array
		Array_T_id_count+=1;
		id=Array_T_id_count;
		data=0;
		Resize(ellements,false);
		Size=ellements;
	}

	Array(u32 ellements,bool zero)
	{
		//initialise array
		Array_T_id_count+=1;
		id=Array_T_id_count;
		data=0;
		Resize(ellements,zero);
		Size=ellements;
	}

	Array()
	{
		//initialise array
		Array_T_id_count+=1;
		id=Array_T_id_count;
		data=0;
		Size=0;
	}


	~Array()
	{
		if  (data)
		{
			#ifdef MEM_ALLOC_TRACE
			printf("WARNING : DESTRUCTOR WITH NON FREED ARRAY [arrayid:%d]\n",id);
			#endif
			Free();
		}
	}
	void SetPtr(T* Source,u32 ellements)
	{
		//initialise array
		Free();
		data=Source;
		Size=ellements;
	}
	T* Resize(u32 size,bool bZero)
	{
		if (size==0)
		{
			if (data)
			{
				#ifdef MEM_ALLOC_TRACE
				printf("Freeing data -> resize to zero[Array:%d]\n",id);
				#endif
				Free();
			}

		}

		if (!data)
			data=(T*)malloc(size*sizeof(T));
		else
			data=(T*)realloc(data,size*sizeof(T));

		//TODO : Optimise this
		//if we allocated more , Zero it out
		if (bZero)
		{
			if (size>Size)
			{
				for (u32 i=Size;i<size;i++)
				{
					u8*p =(u8*)&data[i];
					for (u32 j=0;j<sizeof(T);j++)
					{
						p[j]=0;
					}
				}
			}
		}
		Size=size;

		return data;
	}
	void Zero()
	{
		memset(data,0,sizeof(T)*Size);
	}
	void Free()
	{
		if (Size!=0)
		{
			if (data)
				free(data);
			else
				printf("Data allready freed [Array:%d]\n",id);
			data=0;
		}
		else
		{
			if (data)
				printf("Free : Size=0 , data ptr !=null [Array:%d]\n",id);

		}
	}


	INLINE T& operator [](const u32 i)
    {
#ifdef MEM_BOUND_CHECK
        if (i>=Size)
		{
			printf("Error: Array %d , index out of range (%d>%d)\n",id,i,Size-1);
			MEM_DO_BREAK;
		}
#endif
		return data[i];
    }

	INLINE T& operator [](const s32 i)
    {
#ifdef MEM_BOUND_CHECK
        if (!(i>=0 && i<(s32)Size))
		{
			printf("Error: Array %d , index out of range (%d > %d)\n",id,i,Size-1);
			MEM_DO_BREAK;
		}
#endif
		return data[i];
    }
};




//Simple and fast Stack
template<class T>
class Stack
{
public :
	Array<T> items;
	s32 top;

	INLINE s32 FreeSpace()
	{
		return items.Size-top;
	}
	INLINE T& operator [](const u32 i)
    {
		#ifdef MEM_BOUND_CHECK
		if (i>=top)
		{
			printf("Stack Error : Reading %d item , this item is not used \n",i);
			MEM_DO_BREAK;
		}
		#endif
		return items[i];
    }

	INLINE T& operator [](const s32 i)
    {
		#ifdef MEM_BOUND_CHECK
		if (!(i>0 && i<top))
		{
			printf("Stack Error : Reading %d item , this item is not used \n",i);
			MEM_DO_BREAK;
		}
		#endif
		return items[i];
    }

	Stack()
	{
		top=0;
		items.Resize(10,false);
	}
	Stack(s32 count)
	{
		top=0;
		items.Resize(count,false);
	}
	~Stack()
	{
		items.Free();
	}

	/*void push(T &data)
	{
		if (top>=(s32)items.Size)
		{
			items.Resize(items.Size + items.Size*1/4+2,false);
		}

		items[top++]=data;
	}*/

	void push(T data)
	{
		if (top>=(s32)items.Size)
		{
			items.Resize(items.Size + items.Size*1/4+2,false);
		}

		items[top++]=data;
	}

	void push(T* data)
	{
		if (top>=(s32)items.Size)
		{
			items.Resize(items.Size + items.Size*1/4+2,false);
		}

		items[top++]=data;
	}

	void pop(T* data)
	{
		if (top<=0)
		{
		}
		else
		{
			*data=items[--top];
			if (top<(items.Size/5))
			{
				items.Resize(top*3/2);
			}
		}
	}

	T pop()
	{
		if (top<0)
		{
			//TODO : fix it ... it will work olny for pointers
			return 0;
		}
		else
		{
			if ((u32)top<(items.Size/15))
			{
				items.Resize(top*3/2,false);
			}
			return items[--top];
		}
	}
};


//Fast and simple list
template<class T>
class GrowingList
{
private:
	struct tripl{T item;bool used;};

public:

	Array<tripl> items;

	u32 itemcount;

	INLINE T& operator [](const u32 i)
    {
		#ifdef MEM_BOUND_CHECK
		if (items[i].used==false)
		{
			printf("GrowingList Error : Reading %d item , this item is not used \n",i);
			MEM_DO_BREAK;
		}
		#endif
		return items[i].item;
    }

	INLINE T& operator [](const s32 i)
    {
		#ifdef MEM_BOUND_CHECK
		if (items[i].used==false)
		{
			printf("GrowingList Error : Reading %d item , this item is not used \n",i);
			MEM_DO_BREAK;
		}
		#endif
		return items[i].item;
    }


	GrowingList()
	{
		itemcount=0;
		items.Resize(10,true);
	}
	GrowingList(u32 count)
	{
		itemcount=0;
		items.Resize(count,true);
	}
	~GrowingList()
	{
		items.Free();
	}

	u32 Add(T& item)
	{
		if (itemcount==items.Size)
		{
			items.Resize(items.Size+items.Size*1/4+2,true);
		}
		itemcount++;

		//needs to be optimised
		//TODO: Add a psedo random algo
		for(u32 i=0;i<items.Size;i++)
		{
			if (items[i].used==0)
			{
				items[i].used=1;
				items[i].item=item;
				return i;
			}
		}

		return 0xFFFFFFFF;
	}

	u32 Add(T* item)
	{
		if (itemcount==items.Size)
		{
			items.Resize(items.Size+items.Size*1/4+2,true);
		}
		itemcount++;

		for(u32 i=0;i<items.Size;i++)
		{
			if (items[i].used==0)
			{
				items[i].used=1;
				items[i].item=*item;
				return i;
			}
		}

		return 0xFFFFFFFF;
	}

	bool Remove(u32 item)
	{
		bool rv=items[item].used;
		if (rv)
			items[item].used=0;

		return rv;
	}

};


//faster and simple list (olny for pointer types)
template<class T>
class GrowingListPtr
{
public:
	Array<T*> items;

	u32 itemcount;

	INLINE T*& operator [](const u32 i)
    {
		#ifdef MEM_BOUND_CHECK
		if (items[i]==0)
		{
			printf("GrowingListPtr Error : Reading %d item , this item is not used \n",i);
			MEM_DO_BREAK;
		}
		#endif
		return items[i];
    }

	INLINE T*& operator [](const s32 i)
    {
		#ifdef MEM_BOUND_CHECK
		if (items[i]==0)
		{
			printf("GrowingListPtr Error : Reading %d item , this item is not used \n",i);
			MEM_DO_BREAK;
		}
		#endif
		return items[i];
    }

	GrowingListPtr()
	{
		itemcount=0;
		items.Resize(10,true);
	}
	GrowingListPtr(u32 count)
	{
		itemcount=0;
		items.Resize(count,true);
	}
	~GrowingListPtr()
	{
		items.Free();
	}

	u32 Add(T* item)
	{
#ifdef MEM_BOUND_CHECK
		if (item==0)
		{
			printf("GrowningListPtr Error : Add(T* item) , item ==0\n");
			MEM_DO_BREAK;
		}
#else
	#ifdef MEM_ALLOC_CHECK
		if (item==0)
		{
			printf("GrowningListPtr Error : Add(T* item) , item ==0\n");
			MEM_DO_BREAK;
		}
	#else
		#ifdef TRACE
		if (item==0)
		{
			printf("GrowningListPtr Error : Add(T* item) , item ==0\n");
			TRACE_DO_BREAK;
		}
		#endif
	#endif
#endif

		if (itemcount==items.Size)
		{
			items.Resize(items.Size+items.Size*1/4+2,true);
		}
		itemcount++;

		//TODO: Add a psedo random algo
		for(u32 i=0;i<items.Size;i++)
		{
			if (items[i]==0)
			{
				items[i]=item;
				return i;
			}
		}

		return 0xFFFFFFFF;
	}

	bool Remove(u32 item)
	{
		bool rv=items[item]!=0;
		if (rv)
			items[item]=0;

		return rv;
	}

};

//Fifo list xD
template<class T>
struct Fifo_List
{
	Array<T> Item;
	int ReadIndex;
	int WriteIndex;

	INLINE T GetItem()
	{
		if (!IsEmpty())
			return Item[ReadIndex++];
		else
			WriteIndex=ReadIndex=0;

		return 0;
	}
	INLINE void GetItems(T* items,int count)
	{
		for (int i=0;i<count;i++)
		{
			items[i]=GetItem();
		}
	}
	INLINE void PutItem(T item)
	{
		if (!IsEmpty())
			Item[WriteIndex++]=item;
		else
		{
			if (Item.Size<=WriteIndex)
				Item.Resize(WriteIndex*3/2,false);

			WriteIndex=ReadIndex=0;
			Item[WriteIndex++]=item;
		}
	}
	INLINE void PutItems(T* items,int count)
	{
		for (int i=0;i<count;i++)
		{
			PutItem(items[i]);
		}
	}

	INLINE bool IsEmpty()
	{
		return ReadIndex==WriteIndex;
	}
};


template<class T>
class List : public std::vector<T>
{
public:
	u32 itemcount;
	List()
	{
		itemcount=0;
	}
	INLINE T* Add(T& item)
	{
		push_back(item);
		itemcount++;
		return &(*this)[this->size()-1];
	}
};

//Are these really needed ?
typedef  u32 ThreadEntryFP(void* param);

class cThread
{
private:
	ThreadEntryFP* Entry;
	void* param;
	void* hThread;
public :
	cThread(ThreadEntryFP* function,void* param);
	~cThread();
	//Simple thread functions
	void Start();
	void Suspend();
	void WaitToEnd(u32 msec);
};

//Wait Events
class cResetEvent
{
private:
	void* hEvent;
public :
	cResetEvent(bool State,bool Auto);
	~cResetEvent();
	void Set();		//Set state to signaled
	void Reset();	//Set state to non signaled
	void Wait(u32 msec);//Wait for signal , then reset[if auto]
	void Wait();	//Wait for signal , then reset[if auto]
};


//Dynamic library handler
class cDllHandler
{
private :
	void* lib;
public:
	cDllHandler();
	~cDllHandler();
	bool Load(char* dll);
	bool IsLoaded();
	void Unload();
	void* GetProcAddress(char* name);
};

//Paths
void GetApplicationPath(char* path,u32 size);
//Directory the emulator was launched from, with a trailing separator.
//Everything GetEmuPath() builds (data/dc_boot.bin, data/fsca-table.bin,
//nullDC.cfg, ...) hangs off it.
void SetApplicationPath(const char* path);
char* GetEmuPath(const char* subpath);

class VArray
{
public:

	u8* data;
	u32 size;
	void Init(u32 sz);
	void Term();
	void LockRegion(u32 offset,u32 size);
	void UnLockRegion(u32 offset,u32 size);

	void Zero()
	{
		UnLockRegion(0,size);
		memset(data,0,size);
	}

	INLINE u8& operator [](const u32 i)
    {
#ifdef MEM_BOUND_CHECK
        if (i>=size)
		{
			printf("Error: VArray , index out of range (%d>%d)\n",i,size-1);
			MEM_DO_BREAK;
		}
#endif
		return data[i];
    }
};

class VArray2
{
public:

	u8* data;
	u32 size;
	//void Init(void* data,u32 sz);
	//void Term();
	void LockRegion(u32 offset,u32 size);
	void UnLockRegion(u32 offset,u32 size);

	void Zero()
	{
		UnLockRegion(0,size);
		memset(data,0,size);
	}

	INLINE u8& operator [](const u32 i)
    {
#ifdef MEM_BOUND_CHECK
        if (i>=size)
		{
			printf("Error: VArray2 , index out of range (%d>%d)\n",i,size-1);
			MEM_DO_BREAK;
		}
#endif
		return data[i];
    }
};


int msgboxf(const char* text,unsigned int type,...);