#include "VirtualMachine.h"
#include <string>
#include <iostream>
#include <vector>
#include "Machine.h"
#include <fcntl.h>
#include <unistd.h>
#include <queue>
#include <iostream>
#include <list>
#include <math.h>
#include <string.h>

using namespace std;

extern const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 0;
TVMMemoryPoolID SHARED_MEM = 1;

extern "C"{
	//USED FOR PLACING C CODE STUFF SINCE THIS IS A CPP FILE 
	TVMMainEntry VMLoadModule(const char* module);
	void VMUnloadModule(void);
	void queueing(TVMThreadID thread);

	class TCB{
	public:
		TVMThreadEntry threadEntry;
		TVMThreadPriority priority;
		TVMThreadState state; //needs to be set dead so we can activate it later on
		TVMTick sleepTime;
		TVMThreadID threadID;
		void* Param;
		TVMMemorySize memSize;
		uint8_t* stackBase;
		SMachineContext* mContext;
		int result;
		TVMMutexID mutexID;

		TCB(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
			sleepTime = 0;
			state = VM_THREAD_STATE_DEAD;
			threadEntry = entry;
			Param = param;
			memSize = memsize;
			priority = prio;
			threadID = *tid;
			stackBase = new uint8_t[this->memSize];
			mContext = new SMachineContext();
		}

		TCB(){ ; }
	};

	/*GLOBAL VARS HERE*/
	void callBack(void* param);
	std::vector<TCB*> TCBVec;
	std::queue<TCB*>hPriority;
	std::queue<TCB*>mPriority;
	std::queue<TCB*>lPriority;
	std::vector<TCB*> sleepVec;



	volatile TVMThreadID currRunningTID;
	void Schedule();
	void Skeleton(void *param); //this is what every thread had to run in order to run the function the thread is storing
	void fileCallBack(void* calldata, int result); //callback function for file, when the machine calls this, it means our file operation is done and is ready to be "ran"

	/*END GLOBAL VARS*/

	void idlingThread(void *param)
	{
		MachineEnableSignals();
		while (true){}
	}//our idling thread's function



	TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef state){

		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		if (thread > TCBVec.size() + 1)
			return VM_STATUS_ERROR_INVALID_ID;
		if (state == NULL)
			return VM_STATUS_ERROR_INVALID_PARAMETER;

		*state = TCBVec[thread]->state;
		MachineResumeSignals(&signalSt);
		return VM_STATUS_SUCCESS;
		//check if correct ID later
	}

	void bootupSequence(){
		TCB* idleThread = new TCB();

		idleThread->priority = 0x00;
		idleThread->state = VM_THREAD_STATE_READY;
		idleThread->threadID = 0;
		idleThread->threadEntry = idlingThread;
		idleThread->memSize = 0x100000;
		idleThread->stackBase = new uint8_t[idleThread->memSize];
		idleThread->mContext = new SMachineContext();
		TCBVec.push_back(idleThread);
		MachineContextCreate(idleThread->mContext, idleThread->threadEntry, NULL, idleThread->stackBase, idleThread->memSize);

		TCB* mainThread = new TCB();
		mainThread->priority = VM_THREAD_PRIORITY_NORMAL;
		mainThread->state = VM_THREAD_STATE_RUNNING;
		mainThread->mContext = new SMachineContext();

		mainThread->threadID = 1;
		currRunningTID = 1;
		TCBVec.push_back(mainThread);
	}//this is where we initialize the main thread and idling thread

	TVMStatus VMStart(int tickms, TVMMemorySize heapsize, int machinetickms, TVMMemorySize sharedsize, int argc, char *argv[]){


		TVMMainEntry VMMainFunc = VMLoadModule(argv[0]);
		if (VMMainFunc == NULL){
			return VM_STATUS_FAILURE;
		}
		uint8_t* sharedBase = (uint8_t*)MachineInitialize(machinetickms, sharedsize);
		MachineRequestAlarm((1000 * tickms), callBack, NULL);
		MachineEnableSignals();
		bootupSequence();

		uint8_t* mainBase = new uint8_t[heapsize];
		TVMMemoryPoolID memoryID = 0;
		VMMemoryPoolCreate(mainBase, heapsize, &memoryID);
		//create main pool

		TVMMemoryPoolID sharedID = 1;
		VMMemoryPoolCreate(sharedBase, sharedsize, &sharedID);


		VMMainFunc(argc, argv);
		VMThreadTerminate(0);
		// VMThreadTerminate(1);

		VMUnloadModule();
		// MachineTerminate();
		return VM_STATUS_SUCCESS;

	}//VMStart





	TVMStatus VMThreadSleep(TVMTick tick){
		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		if (tick == VM_TIMEOUT_INFINITE) //if we get a time out
			return VM_STATUS_ERROR_INVALID_PARAMETER;


		else if (tick == VM_TIMEOUT_IMMEDIATE){

			if (TCBVec[currRunningTID]->priority == VM_THREAD_PRIORITY_HIGH){
				hPriority.push(TCBVec[currRunningTID]);
				if (TCBVec[currRunningTID]->threadID != (hPriority.front())->threadID){
					currRunningTID = (hPriority.front())->threadID;
					//FORGOT TO DO TCBVec[currRunningTID].state = RUNNING
					hPriority.pop();
					MachineContextSwitch(TCBVec[currRunningTID]->mContext, (hPriority.front())->mContext);
				}
				else
					hPriority.pop();
			}
			else if (TCBVec[currRunningTID]->priority == VM_THREAD_PRIORITY_NORMAL){
				mPriority.push(TCBVec[currRunningTID]);
				if (TCBVec[currRunningTID]->threadID != (mPriority.front())->threadID){
					currRunningTID = (mPriority.front())->threadID;
					//FORGOT TO DO TCBVec[currRunningTID].state = RUNNING
					mPriority.pop();
					MachineContextSwitch(TCBVec[currRunningTID]->mContext, (mPriority.front())->mContext);
				}
				else
					mPriority.pop();
			}
			else{
				lPriority.push(TCBVec[currRunningTID]);
				if (TCBVec[currRunningTID]->threadID != (lPriority.front())->threadID){
					currRunningTID = (lPriority.front())->threadID;
					//FORGOT TO DO TCBVec[currRunningTID].state = RUNNING
					lPriority.pop();
					MachineContextSwitch(TCBVec[currRunningTID]->mContext, (lPriority.front())->mContext);
				}
				else
					lPriority.pop();
			}//if we do sleep(VMtimeoutimmediate, the current running thread should switch to a thread of equal priority, 
			//doesn't matter if it's low and theres a high waiting, it will still switch to a low thread.). If there aren't any threada os equal priority running, it continues runnin

			MachineResumeSignals(&signalSt);
			return VM_STATUS_SUCCESS;
		}//immediate scheduler

		TCBVec[currRunningTID]->sleepTime = tick;
		TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;
		sleepVec.push_back(TCBVec[currRunningTID]);//otherwise we place our thread to sleep, and psuh it into our sleep vec and look for next available ready thread with highest priority 
		Schedule();
		MachineResumeSignals(&signalSt);

		return VM_STATUS_SUCCESS;
	}

	void callBack(void* param){
		for (int i = 0; i < (int)sleepVec.size(); i++){
			if (TCBVec[i]->state == VM_THREAD_STATE_RUNNING){
				TCBVec[i]->state = VM_THREAD_STATE_READY;
				if (TCBVec[i]->priority == VM_THREAD_PRIORITY_HIGH)
					hPriority.push(TCBVec[i]);
				else if (TCBVec[i]->priority == VM_THREAD_PRIORITY_NORMAL){
					mPriority.push(TCBVec[i]);
				}
				else if (TCBVec[i]->priority == VM_THREAD_PRIORITY_LOW)
					lPriority.push(TCBVec[i]);
			}//if the thread is running, we set it to ready, and place it back to it's respective queue then schedule because this is round robin.




			if (sleepVec[i]->sleepTime > 0){
				sleepVec[i]->sleepTime--;
				if (sleepVec[i]->sleepTime == 0){
					sleepVec[i]->state = VM_THREAD_STATE_READY;
					if (sleepVec[i]->priority == VM_THREAD_PRIORITY_HIGH)
						hPriority.push(sleepVec[i]);
					else if (sleepVec[i]->priority == VM_THREAD_PRIORITY_NORMAL){
						mPriority.push(sleepVec[i]);
					}
					else
						lPriority.push(sleepVec[i]);
					//this body pushes the sleeping thread into a ready queue

					sleepVec.erase(sleepVec.begin() + i);
					//then we have to remove the thread from the sleeping vector
					i--;
				}
			}//checks sleepVector and tries to wake up sleeping threads. When awoken, they get put into a ready queue

		}
		Schedule();

	}//callBack is called to check for sleeping threads. It is also used to switch the running thread so that every thread has a chance to run instead of being blocked by one long running thread.

	TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){

		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		if (tid == NULL || entry == NULL)
			return VM_STATUS_ERROR_INVALID_PARAMETER;

		*tid = TCBVec.size();

		TCBVec.push_back((new TCB(entry, param, memsize, prio, tid)));

		MachineResumeSignals(&signalSt);

		return VM_STATUS_SUCCESS;
	}//creates a thread that will hold a function 

	TVMStatus VMThreadActivate(TVMThreadID thread){
		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		if (thread > TCBVec.size() - 1)//if invalid thread
			return VM_STATUS_ERROR_INVALID_ID;

		TCB *activatedThread = TCBVec[thread];

		if (activatedThread->state != VM_THREAD_STATE_DEAD)
			return VM_STATUS_ERROR_INVALID_STATE;

		MachineContextCreate(activatedThread->mContext, Skeleton, activatedThread->Param, activatedThread->stackBase, activatedThread->memSize);

		activatedThread->state = VM_THREAD_STATE_READY;

		if (TCBVec[thread]->priority == VM_THREAD_PRIORITY_HIGH)
			hPriority.push(TCBVec[thread]);

		else if (TCBVec[thread]->priority == VM_THREAD_PRIORITY_NORMAL)
			mPriority.push(TCBVec[thread]);

		else
			lPriority.push(TCBVec[thread]);
		//push into its respective queue

		Schedule();


		MachineResumeSignals(&signalSt);
		return VM_STATUS_SUCCESS;
	}//activates a created thread that's initially dead and places it into its respective ready queue

	TVMStatus VMThreadTerminate(TVMThreadID thread){
		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		if (thread > TCBVec.size() + 1)
			return VM_STATUS_ERROR_INVALID_ID;
		else if (thread == VM_THREAD_STATE_DEAD)
			return VM_STATUS_ERROR_INVALID_STATE;

		TCBVec[thread]->state = VM_THREAD_STATE_DEAD;
		Schedule();

		MachineResumeSignals(&signalSt);
		return VM_STATUS_SUCCESS;
	}//when thread is done running all its functions, it gets terminated(no point of keeping it arounf when we can allocate its resources to a thread that may need it)

	void Schedule(){
		int oldRunningTID = currRunningTID;

		if (TCBVec[currRunningTID]->state == VM_THREAD_STATE_RUNNING){//nonQuantum scheduling due to threadactivate, meaning we'll go into this statement when we schedule after activating a thread
			//we will only switch threads if we schedule an activated thread that has higher priority (ONLY HIGHER)
			if (TCBVec[currRunningTID]->priority < VM_THREAD_PRIORITY_HIGH && !hPriority.empty()){//if our current thread is lower than high and high Q not empty (med/low/idle)
				if (TCBVec[oldRunningTID]->priority == VM_THREAD_PRIORITY_NORMAL)
					mPriority.push(TCBVec[oldRunningTID]);
				else if (TCBVec[oldRunningTID]->priority == VM_THREAD_PRIORITY_LOW)
					lPriority.push(TCBVec[oldRunningTID]);

				currRunningTID = (hPriority.front())->threadID;
				(hPriority.front())->state = VM_THREAD_STATE_RUNNING;
				hPriority.pop();
				MachineContextSwitch(TCBVec[oldRunningTID]->mContext, TCBVec[currRunningTID]->mContext);


			}
			else if (TCBVec[currRunningTID]->priority < VM_THREAD_PRIORITY_NORMAL && !mPriority.empty()){//if our current thread is lower than medium (low/idle)
				if (TCBVec[oldRunningTID]->priority == VM_THREAD_PRIORITY_LOW)
					lPriority.push(TCBVec[oldRunningTID]);

				currRunningTID = (mPriority.front())->threadID;
				(mPriority.front())->state = VM_THREAD_STATE_RUNNING;
				mPriority.pop();
				MachineContextSwitch(TCBVec[oldRunningTID]->mContext, TCBVec[currRunningTID]->mContext);
			}
			else if (TCBVec[currRunningTID]->priority < VM_THREAD_PRIORITY_LOW && !lPriority.empty()){//we're running on a idle priority thread but there aren't any higher priority threads to be run
				currRunningTID = (lPriority.front())->threadID;
				(lPriority.front())->state = VM_THREAD_STATE_RUNNING;
				lPriority.pop();
				MachineContextSwitch(TCBVec[oldRunningTID]->mContext, TCBVec[currRunningTID]->mContext);
			}
		}//nonQuantum Switch

		else{//quantum scheduling from callBack function or from sleep, meaning we'll switch threads if we can find a ready thread of EQUAL or HIGHER priority. 
			if (!hPriority.empty()){//check highest priority queue
				currRunningTID = (hPriority.front())->threadID;
				(hPriority.front())->state = VM_THREAD_STATE_RUNNING;
				hPriority.pop();
				MachineContextSwitch(TCBVec[oldRunningTID]->mContext, TCBVec[currRunningTID]->mContext);
			}
			else if (!mPriority.empty()){
				currRunningTID = (mPriority.front())->threadID;
				(mPriority.front())->state = VM_THREAD_STATE_RUNNING;
				mPriority.pop();
				MachineContextSwitch(TCBVec[oldRunningTID]->mContext, TCBVec[currRunningTID]->mContext);

			}
			else if (!lPriority.empty()){
				currRunningTID = (lPriority.front())->threadID;
				(lPriority.front())->state = VM_THREAD_STATE_RUNNING;
				lPriority.pop();
				MachineContextSwitch(TCBVec[oldRunningTID]->mContext, TCBVec[currRunningTID]->mContext);
			}
			else{
				currRunningTID = 0;
				TCBVec[0]->state = VM_THREAD_STATE_RUNNING;
				MachineContextSwitch(TCBVec[oldRunningTID]->mContext, TCBVec[0]->mContext);
			}
		}
	}



	void Skeleton(void* param){
		MachineEnableSignals();
		TCBVec[currRunningTID]->threadEntry(TCBVec[currRunningTID]->Param);
		VMThreadTerminate(currRunningTID);
	}//enables signals for the thread and runs the function() that the current thread holds

	void fileCallBack(void* calldata, int result){

		TCB* temp = (TCB*)calldata;
		TVMThreadID tempTID = temp->threadID;
		temp->result = result;
		temp->state = VM_THREAD_STATE_READY;

		if (temp->priority == VM_THREAD_PRIORITY_HIGH)
			hPriority.push(TCBVec[tempTID]);
		else if (temp->priority == VM_THREAD_PRIORITY_NORMAL)
			mPriority.push(temp);
		else
			lPriority.push(TCBVec[tempTID]);
		Schedule();
	}

	TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
		if (filename == NULL || filedescriptor == NULL)
			return VM_STATUS_ERROR_INVALID_PARAMETER;

		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;
		MachineFileOpen(filename, flags, mode, fileCallBack, TCBVec[currRunningTID]);
		Schedule();

		*filedescriptor = TCBVec[currRunningTID]->result;
		MachineResumeSignals(&signalSt);
		return VM_STATUS_SUCCESS;
	}


	TVMStatus VMFileClose(int filedescriptor){
		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;
		MachineFileClose(filedescriptor, fileCallBack, TCBVec[currRunningTID]);
		Schedule();
		if (TCBVec[currRunningTID]->result < 0)
			return VM_STATUS_FAILURE;
		MachineResumeSignals(&signalSt);
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);

		TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;
		MachineFileSeek(filedescriptor, offset, whence, fileCallBack, TCBVec[currRunningTID]);
		Schedule();
		if (TCBVec[currRunningTID]->result < 0)
			return VM_STATUS_FAILURE;
		*newoffset = TCBVec[currRunningTID]->result;
		MachineResumeSignals(&signalSt);
		return VM_STATUS_SUCCESS;

	}





	
	class mutex {
	public:
	TVMThreadID owner;

	std::vector<TCB*> waiter;

	TVMMutexIDRef mutexref;
	int mutexLock; //0 unlocked, 1 locked

	mutex(TVMMutexIDRef mutexref){
	mutexLock = 0;
	this->mutexref = mutexref;
	}
	};

	std::queue<TCB*> lPrioMutex;
	std::queue<TCB*> mPrioMutex;
	std::queue<TCB*> hPrioMutex;
	std::vector<TCB*> sleepMutex;
	std::vector<TCB*> threadsMutex;
	std::vector<mutex*> mutexVec;

	TVMStatus VMMutexCreate(TVMMutexIDRef mutexref) {
	TMachineSignalState signalSt;
	MachineSuspendSignals(&signalSt);
	if (mutexref == NULL) {
	return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	mutex *newMutex = new mutex(mutexref);
	mutexVec.push_back(newMutex);
	MachineResumeSignals(&signalSt);
	return VM_STATUS_SUCCESS;
	}

	void queueThreadForMutex(TVMThreadID thread) {
	TCBVec[thread]->state = VM_THREAD_STATE_READY;
	if (TCBVec[thread]->priority == VM_THREAD_PRIORITY_HIGH)
	hPrioMutex.push(TCBVec[thread]);
	else if (TCBVec[thread]->priority == VM_THREAD_PRIORITY_NORMAL)
	mPrioMutex.push(TCBVec[thread]);
	else if (TCBVec[thread]->priority == VM_THREAD_PRIORITY_LOW)
	lPrioMutex.push(TCBVec[thread]);
	}


	TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout) {
	TMachineSignalState signalSt;
	MachineSuspendSignals(&signalSt);
	if (timeout == VM_TIMEOUT_INFINITE){
	MachineResumeSignals(&signalSt);
	return VM_STATUS_FAILURE;
	}
	if (mutex >= mutexVec.size()) {
	MachineResumeSignals(&signalSt);
	return VM_STATUS_ERROR_INVALID_ID;
	}
	if (timeout == VM_TIMEOUT_IMMEDIATE){
	if (mutexVec[mutex]->mutexLock == 1) {
	MachineResumeSignals(&signalSt);
	return VM_STATUS_FAILURE;
	}

	else {
	//if(owner=1, whats current thread doing, is it ready?)
	mutexVec[mutex]->mutexLock = 1;

	//
	}

	}
	else {//if locked, pass thread into proper prio q for mutex
	//set to wating, set ticks=timeout
	//pas in to sleeping q, call sched
	//if free, set current owner to current thread
	if (mutexVec[mutex]->mutexLock == 1) {
	TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;//waiting
	if (timeout != VM_TIMEOUT_INFINITE) {
	TCBVec[currRunningTID]->sleepTime = timeout;
	sleepMutex.push_back(TCBVec[currRunningTID]);
	queueThreadForMutex(currRunningTID);

	}
	}
	}
	Schedule();
	MachineResumeSignals(&signalSt);
	return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMutexRelease(TVMMutexID mutex) {
	//cehck qs high to low, if isnt empty, pop front
	//make that owner, set that thread to ready and push into ready q
	//call sched
	TMachineSignalState signalSt;
	MachineSuspendSignals(&signalSt);
	MachineResumeSignals(&signalSt);
	return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMutexDelete(TVMMutexID mutex) {
	TMachineSignalState signalSt;
	MachineSuspendSignals(&signalSt);

	mutexVec.erase(mutexVec.begin() + mutex);

	MachineResumeSignals(&signalSt);
	return VM_STATUS_SUCCESS;
	}

	

	class Block{
	public:
		unsigned int offset;
		uint8_t* address;
		Block(TVMMemorySize size, uint8_t* base){
			this->offset = size;
			this->address = base;
		};//initializes our block 
	};

	class MemoryPool{
	public:
		list <Block> allocatedMem;
		list <Block> freeMem;

		uint8_t* mBase;
		TVMMemorySize mPoolSize;
		TVMMemoryPoolID mPoolID;
		TVMMemorySize mSize;

		MemoryPool(TVMMemorySize size, uint8_t* base){
			Block *newBlock = new Block(size, base);
			(this->freeMem).push_back(*newBlock);
		}//when we initialize the mempool the freeMemory's base and size = mempool since we haven't allocated any memory.
	};

	vector<MemoryPool> globalMemPool;

	TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory){

		//if (base == NULL || memory == NULL || size == 0)
		//return VM_STATUS_ERROR_INVALID_PARAMETER;

		MemoryPool* newPool = new MemoryPool(size, (uint8_t *)base);
		newPool->mBase = (uint8_t *)base;
		newPool->mSize = size;
		newPool->mPoolID = *memory;
		*memory = globalMemPool.size();
		//cerr << "new pool has a size of: " << size << endl;
		globalMemPool.push_back(*newPool);

		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory){
		if (memory >= globalMemPool.size())
			return VM_STATUS_ERROR_INVALID_PARAMETER;

		globalMemPool.erase(globalMemPool.begin() + memory);

		return VM_STATUS_SUCCESS;
	}

	TVMMemorySize queryFreeSpace(TVMMemoryPoolID memory){
		TVMMemorySize freeSpace = 0;
		list<Block>::iterator listIter = globalMemPool[memory].freeMem.begin();

		while (listIter != globalMemPool[memory].freeMem.end()){
			freeSpace += listIter->offset;
			++listIter;
		}

		return freeSpace;
	}//checks how much free space is left in a specific memory pool

	TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft){
		if (memory >= globalMemPool.size() || bytesleft ==  NULL)
			return VM_STATUS_ERROR_INVALID_PARAMETER;

		*bytesleft = queryFreeSpace(memory);

		return VM_STATUS_SUCCESS;
	}

	bool firstAddress(const Block & a, const Block & b) { return a.address < b.address; }

	TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer){
		if (memory >= globalMemPool.size() || size == 0 || pointer == NULL)
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		if (queryFreeSpace(memory) < size)
			return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;

		TVMMemorySize remaining = 64;
		TVMMemorySize allocSize = ((size + remaining - 1) / remaining) * remaining;
		//rounds up to multiple of 64

		list<Block>::iterator listIter = globalMemPool[memory].freeMem.begin();

		//cerr << "WE ARE TRYING TO ALLOCATE " << size << " bytes" << endl;

		while (listIter != globalMemPool[memory].freeMem.end()){
			if (allocSize <= listIter->offset){//if we find spot that we can fit in
				*pointer = listIter->address;
				Block *allocBlock = new Block(size, listIter->address);//create a new block for the allocMem list
				globalMemPool[memory].allocatedMem.push_back(*allocBlock);
				Block tempBlock = *listIter;
				if (listIter->offset > size){
					tempBlock.address = tempBlock.address + size;
					tempBlock.offset = tempBlock.offset - size;
					globalMemPool[memory].freeMem.push_back(tempBlock);
				}//if we allocate mem thats smaller than the first fit chunk, we'll want to break that chunk up

				listIter = globalMemPool[memory].freeMem.erase(listIter);
				globalMemPool[memory].freeMem.sort(firstAddress);
				break;
			}//if we find a chunk of free memory that is larger than the space we want
			++listIter;
		}//checks for first fit

		/*
		cerr << "THIS IS WHAT'S LEFT IN THE FREE MEM LIST:" << endl;
		listIter = globalMemPool[memory].freeMem.begin();
		while (listIter != globalMemPool[memory].freeMem.end()){
		cerr << "address: " << static_cast<void *>(listIter->address) << "   length: " << listIter->offset << endl;
		++listIter;
		}
		cerr << endl;

		cerr << "THIS IS WHAT IS IN ALLOCATED LIST" << endl;
		listIter = globalMemPool[memory].allocatedMem.begin();
		while (listIter != globalMemPool[memory].allocatedMem.end()){
		cerr << "address: " << static_cast<void *>(listIter->address) << "   length: " << listIter->offset << endl;
		listIter++;
		}
		cerr << endl;
		*///This is to check if I'm allocating correctly
		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer){
		if (memory >= globalMemPool.size() || pointer == NULL)
			return VM_STATUS_ERROR_INVALID_PARAMETER;

		//need to deal with error checking if pointer points to allocated mem location from the memory pool
		list<Block>::iterator listIter = globalMemPool[memory].allocatedMem.begin();

		while (listIter != globalMemPool[memory].allocatedMem.end()){
			if (listIter->address == pointer){
				globalMemPool[memory].freeMem.push_back(*listIter);
				globalMemPool[memory].freeMem.sort(firstAddress);
				//cerr << "We are deallocating " << listIter->offset<< " bytes" << endl;
				listIter = globalMemPool[memory].allocatedMem.erase(listIter);
				//these 3 lines find the block we want to deallocate and pushes it back in to the free Mem list
				break;
			}//once we find the memory that we want to deallocate
			listIter++;
		}
		//sort here
		listIter = globalMemPool[memory].freeMem.begin();
		while (listIter != globalMemPool[memory].freeMem.end()){
			list<Block>::iterator boundsChecker = listIter;
			boundsChecker++;
			//cerr << "HANG 3" << endl;
			if (boundsChecker != globalMemPool[memory].freeMem.end()){
				if (boundsChecker->address == listIter->address + listIter->offset){
					Block* contiguousBlock = new Block((listIter->offset + boundsChecker->offset), listIter->address);
					globalMemPool[memory].freeMem.push_back(*contiguousBlock);
					globalMemPool[memory].freeMem.erase(boundsChecker);
					globalMemPool[memory].freeMem.erase(listIter);
					globalMemPool[memory].freeMem.sort(firstAddress);
					listIter = globalMemPool[memory].freeMem.begin();

				}
				listIter++;
			}//need to make sure we're not checking past the list or else nasty seg fault errors
			else{
				listIter++;
			}
		}

		/*
		cerr << "DEALLOCATING ADDRESS: " << static_cast<void *>(pointer) << endl;
		cerr << "THIS IS WHAT'S LEFT IN THE FREE MEM LIST:" << endl;
		listIter = globalMemPool[memory].freeMem.begin();
		while (listIter != globalMemPool[memory].freeMem.end()){
		cerr << "address: " << static_cast<void *>(listIter->address) << "   length: " << listIter->offset << endl;
		++listIter;
		}
		cerr << endl;

		cerr << "THIS IS WHAT IS IN ALLOCATED LIST" << endl;
		listIter = globalMemPool[memory].allocatedMem.begin();
		while (listIter != globalMemPool[memory].allocatedMem.end()){
		cerr << "address: " << static_cast<void *>(listIter->address) << "   length: " << listIter->offset << endl;
		listIter++;
		}
		cerr << endl;
		*/

		return VM_STATUS_SUCCESS;
	}

	TVMStatus VMFileWrite(int filedescriptor, void* data, int* length){
		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);
		if (data == NULL || length == NULL)//if invalid parameter input
			return VM_STATUS_ERROR_INVALID_PARAMETER;
		float totalLength = *length;
		float iterations = ceil(totalLength / 512);
		int writeLength = 0;

		void* base;
		
		for (int i = 0; i < (int) iterations; i++){
			VMMemoryPoolAllocate(SHARED_MEM, 512, &base);
			TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;
			if (totalLength - 512 < 0){
				MachineFileWrite(filedescriptor, base, (int)totalLength, fileCallBack, TCBVec[currRunningTID]);
				memcpy(base, (uint8_t*)data + (i * 512), (int)totalLength);
			}
			else{
				MachineFileWrite(filedescriptor, base, 512, fileCallBack, TCBVec[currRunningTID]);
				memcpy(base, (uint8_t*)data + (i * 512), 512);
			}
			
			Schedule();

			writeLength += TCBVec[currRunningTID]->result;
			VMMemoryPoolDeallocate(SHARED_MEM, base);
			totalLength -= 512; //this is to check how big of a chunk we have remaining to write
		}
		
		/*
		VMMemoryPoolAllocate(SHARED_MEM, (TVMMemorySize)*length, &base);
		memcpy(base, data, *length);
		
		Ffi
		TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;
		MachineFileWrite(filedescriptor, base, *length, fileCallBack, TCBVec[currRunningTID]);
		
		if (TCBVec[currRunningTID]->result < 0)
			return VM_STATUS_FAILURE; // if we can't write

		Schedule();

		*length = TCBVec[currRunningTID]->result;

		VMMemoryPoolDeallocate(SHARED_MEM, base);
		*/
		
		*length = writeLength;
		MachineResumeSignals(&signalSt);
	


		return VM_STATUS_SUCCESS;
	}//when called, it'll write and store the # of bytes written into our TCB's struct result, which will be placed into our int*length and be retrieved by our file.so functions (take a look at file.c and how it sets its own local variables to our vmfile functions)

	TVMStatus VMFileRead(int filedescriptor, void *data, int* length){
		TMachineSignalState signalSt;
		MachineSuspendSignals(&signalSt);
		void *base;

		if (data == NULL || length == NULL)
			return VM_STATUS_ERROR_INVALID_PARAMETER;

		float totalLength = *length;
		float iterations = ceil(totalLength / 512);
		int readLength = 0;

		for (int i = 0; i < (int)iterations; i++){
			TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;
			VMMemoryPoolAllocate(SHARED_MEM, 512, &base);

			if (totalLength - 512 < 0){
				MachineFileRead(filedescriptor, base, totalLength, fileCallBack, TCBVec[currRunningTID]);
				Schedule();
				memcpy((uint8_t*)data + (i * 512), base, (int)totalLength);
			}
			else{
				MachineFileRead(filedescriptor, base, 512, fileCallBack, TCBVec[currRunningTID]);
				Schedule();
				memcpy((uint8_t*)data + (i * 512), base , 512);
			}
			readLength += TCBVec[currRunningTID]->result;
			VMMemoryPoolDeallocate(SHARED_MEM, base);
			totalLength -= 512;
		}


		/*
		TCBVec[currRunningTID]->state = VM_THREAD_STATE_WAITING;

		VMMemoryPoolAllocate(SHARED_MEM, (TVMMemorySize)*length, &base);
		MachineFileRead(filedescriptor, base, *length, fileCallBack, TCBVec[currRunningTID]);

		Schedule();
		*length = TCBVec[currRunningTID]->result;

		memcpy(data, base, *length);

		VMMemoryPoolDeallocate(SHARED_MEM, base);
		*/

		*length = readLength;
		MachineResumeSignals(&signalSt);
		return VM_STATUS_SUCCESS;
	}
}
//need to change vmstart


