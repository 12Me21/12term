#include "xftint.h"

static struct {
	const char *name;
	int alloc_count;
	int alloc_mem;
	int free_count;
	int free_mem;
} XftInUse[XFT_MEM_NUM] = {
	{ "XftDraw", 0, 0 },
	{ "XftFont", 0 ,0 },
	{ "XftFtFile", 0, 0 },
	{ "XftGlyph", 0, 0 },
};

static int XftAllocCount, XftAllocMem;
static int XftFreeCount, XftFreeMem;

static const int XftMemNotice = 1*1024*1024;

static int XftAllocNotify, XftFreeNotify;

void XftMemReport(void) {
	print("Xft Memory Usage:\n");
	print("\t   Which       Alloc           Free\n");
	print("\t           count   bytes   count   bytes\n");
	for (int i = 0; i < XFT_MEM_NUM; i++)
		print("\t%8.8s%8d%8d%8d%8d\n",
		      XftInUse[i].name,
		      XftInUse[i].alloc_count, XftInUse[i].alloc_mem,
		      XftInUse[i].free_count, XftInUse[i].free_mem);
	print("\t%8.8s%8d%8d%8d%8d\n",
	      "Total",
	      XftAllocCount, XftAllocMem,
	      XftFreeCount, XftFreeMem);
	XftAllocNotify = 0;
	XftFreeNotify = 0;
}

void XftMemAlloc(int kind, int size) {
	if (XftDebug() & XFT_DBG_MEMORY) {
		XftInUse[kind].alloc_count++;
		XftInUse[kind].alloc_mem += size;
		XftAllocCount++;
		XftAllocMem += size;
		XftAllocNotify += size;
		if (XftAllocNotify > XftMemNotice)
			XftMemReport();
	}
}

void* XftMalloc(int kind, size_t size) {
	void* m = malloc(size);
	if (m)
		XftMemAlloc(kind, size);
	return m;
}

void XftMemFree(int kind, int size) {
	if (XftDebug() & XFT_DBG_MEMORY) {
		XftInUse[kind].free_count++;
		XftInUse[kind].free_mem += size;
		XftFreeCount++;
		XftFreeMem += size;
		XftFreeNotify += size;
		if (XftFreeNotify > XftMemNotice)
			XftMemReport();
	}
}
