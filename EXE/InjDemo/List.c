/*******************************************************************
 * List.c                                                          *
 * Linked list functions.                                          *
 *                                                                 *
 * (c) A. Miguel Feijao, 4/8/2005                                  *
 *******************************************************************/

#include <windows.h>
#include <stdlib.h>

#include "List.h"

/***********************************
 * Insert a new item into the List *
 ***********************************/
void ListInsert(PLIST *pList, PLIST pNew)
{
    pNew->pNext = *pList;
    *pList = pNew;
}

/****************************************
 * Insert a new item into List.         *
 * The List will be sorted according to *
 * the criteria defined by CmpFunc().   *
 ****************************************/
void ListInsertAndSort(PLIST *pList, PLIST pNew, DWORD Param, CMPFUNC CmpFunc)
{
    PLIST pCurrent, pPrevious;

    pCurrent = pPrevious = *pList;

    while (pCurrent)
    {
        if (CmpFunc(pCurrent, Param))
        {
            // Insert in 1st position
            if (pCurrent == *pList)
            {
                pNew->pNext = pCurrent;
                *pList = pNew;
            }
            // Insert in the middle
            else
            {
                pPrevious->pNext = pNew;
                pNew->pNext = pCurrent;
            }

            return;
        }

        pPrevious = pCurrent;
        pCurrent = pCurrent->pNext;
    }

    // List is empty
    if (*pList == NULL)
    {
        *pList = pNew;
        pNew->pNext = NULL;
    }
    // Insert in the last position
    else
    {
        pPrevious->pNext = pNew;
        pNew->pNext = NULL;
    }
}

/*****************************
 * Delete one item from List *
 *****************************/
void ListDelete(PLIST *pList, DWORD Param, CMPFUNC CmpFunc)
{
    PLIST pAux;
    PLIST p = *pList;

    while (p)
    {
        if (CmpFunc(p, Param))
        {
            pAux = p;
            *pList = p->pNext;
            free(pAux);
            return;
        }
        p = p->pNext;
    }
}

/****************************
 * Delete all items in List *
 ****************************/
void ListDeleteAll(PLIST *pList)
{
    PLIST pAux;
    PLIST p = *pList;

    while (p)
    {
        pAux = p;
        p = p->pNext;
        free(pAux);
    }

    *pList = NULL;
}

/************************
 * Find an item in List *
 ************************/
PLIST ListFind(PLIST pList, DWORD Param, CMPFUNC CmpFunc)
{
    while (pList)
    {
        if (CmpFunc(pList, Param))
            return pList;
        pList = pList->pNext;
    }
    return NULL; // Not found
}

