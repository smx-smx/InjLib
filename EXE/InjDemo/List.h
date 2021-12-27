#ifndef __LIST_H__
#define __LIST_H__

typedef struct _LIST{
    struct  _LIST *pNext;
} LIST, *PLIST;

typedef BOOL (*CMPFUNC)(PLIST, DWORD);

void ListInsert(PLIST *pList, PLIST pNew);
void ListInsertAndSort(PLIST *pList, PLIST pNew, DWORD Param, CMPFUNC CmpFunc);
void ListDelete(PLIST *pList, DWORD Param, CMPFUNC CmpFunc);
void ListDeleteAll(PLIST *pList);
PLIST ListFind(PLIST pList, DWORD Param, CMPFUNC CmpFunc);

#endif // __LIST_H__
