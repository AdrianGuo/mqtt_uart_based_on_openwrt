/*******************************************************************************
 * Copyright (c) 2009, 2013 IBM Corp.
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
 *    Ian Craggs - initial implementation and documentation
 *******************************************************************************/

/** @file
 * \brief functions which apply to tree structures.
 *
 * These trees can hold data of any sort, pointed to by the content pointer of the
 * Node structure.
 * */
 
/*
���������������������Щ���Ƕ�����
��ҪΪ����ͼ�����Ч�ʡ�
�ټ���Ӧ�õķ��棺
����������룬�����������������(STL��map�Ļ���)

�������Red Black Tree�� ��һ����ƽ���������������ڼ������ѧ���õ���һ�����ݽṹ�����͵���;��ʵ�ֹ������顣
���ں����Ҳ�Ƕ�������������ǵ���ÿһ���ڵ�ıȽ�ֵ��������ڻ����������������
�е����нڵ㣬����С�ڻ�����������������е����нڵ㡣��ȷ�����������ʱ�ܹ�����
�������в��Ҹ�����ֵ��
������ں���ʽ�����Ҳ�ر����ã���������������õĳ־����ݽṹ֮һ������������
���������ͼ��ϣ���ͻ��֮�������ܱ���Ϊ��ǰ�İ汾������O(log n)��ʱ��֮�⣬���
���ĳ־ð汾��ÿ�β����ɾ����ҪO(log n)�Ŀռ䡣
��ߵ��� < �ұߵ���

�������һ�����򷽷�,Ȼ���ڴ洢��ʱ��������������,���ս���úܺõĲ���Ч��
Ҷ�ӽڵ㶼�ǿսڵ���Ϊ��˵��������,���ղ�ʵ�ʲ���,������һ���м����
������������ɫƽ��ģ�����������ӵ�������·���ϵĺ�����������ͬ��
����̫��Ҫ��,һ��һ��Ҫע�ⷽ��,���������°빦��.
������ת�������������ڵ����λ��,����.
ֻҪ��ߵľ���Ĭ�Ϻ�ɫ����,Ȼ�����жϺ�����,Ȼ���ٵ���.

������������һ�����ݽṹ,Ŀ���ǿ��ٲ���,ֻ��Ϊ��ʵ��������Ŀ��,��Ҫ���Ѿ���ȥά��������ݽṹ.
*/ 

#define NO_HEAP_TRACKING 1

#include "Tree.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <memory.h>

#include "Heap.h"


void TreeInitializeNoMalloc(Tree* aTree, int(*compare)(void*, void*, int))	//-����һ���ڵ�
{
	memset(aTree, '\0', sizeof(Tree));
	aTree->heap_tracking = 1;		//-һ����־λ,��ʾ�Ƿ��и��ٹ���
	aTree->index[0].compare = compare;
	aTree->indexes = 1;	//-�����ʱ��������Ϊ1
}

/**
 * Allocates and initializes a new tree structure.
 * @return a pointer to the new tree structure
 */
Tree* TreeInitialize(int(*compare)(void*, void*, int))	//-�����ͳ�ʼ����һ��tree�ṹ��Ԫ��
{
#if defined(UNIT_TESTS)
	Tree* newt = malloc(sizeof(Tree));
#else
	Tree* newt = mymalloc(__FILE__, __LINE__, sizeof(Tree));	//-__FILE__����ָʾ�����������Դ�ļ����ļ���;__LINE__����ָʾ���������Դ�ļ��е�λ����Ϣ
#endif
	TreeInitializeNoMalloc(newt, compare);
	return newt;
}


void TreeAddIndex(Tree* aTree, int(*compare)(void*, void*, int))	//-����������������,��һ����֧,������һ���򵥵ĳ�Ա
{
	aTree->index[aTree->indexes].compare = compare;
	++(aTree->indexes);	//-����������
}


void TreeFree(Tree* aTree)	//-�ͷ�һ���ڵ�
{
#if defined(UNIT_TESTS)
	free(aTree);
#else
	(aTree->heap_tracking) ? myfree(__FILE__, __LINE__, aTree) : free(aTree);
#endif
}


#define LEFT 0
#define RIGHT 1
#if !defined(max)
#define max(a, b) (a > b) ? a : b;
#endif



int isRed(Node* aNode)	//-�ж��Ƿ���,�з���1
{
	return (aNode != NULL) && (aNode->red);
}


int isBlack(Node* aNode)	//-�ж��Ƿ��,�շ���1
{
	return (aNode == NULL) || (aNode->red == 0);
}

//-�������
//-���ȷ��ʸ���������������ң������������������ң�������
int TreeWalk(Node* curnode, int depth)	//-����һ����ֵ��ȥѧϰ�ĺ���,�Լ������Լ�,�ݹ����
{
	if (curnode)
	{
		int left = TreeWalk(curnode->child[LEFT], depth+1);
		int right = TreeWalk(curnode->child[RIGHT], depth+1);
		depth = max(left, right);
		if (curnode->red)
		{
			/*if (isRed(curnode->child[LEFT]) || isRed(curnode->child[RIGHT]))
			{
				printf("red/black tree violation %p\n", curnode->content);
				exit(-99);
			}*/;
		}
	}
	return depth;
}


int TreeMaxDepth(Tree *aTree)
{
	int rc = TreeWalk(aTree->index[0].root, 0);	//-��������һ��
	/*if (aTree->root->red)
	{
		printf("root node should not be red %p\n", aTree->root->content);
		exit(-99);
	}*/
	return rc;
}

//-һ����ʱ����otherָ�������ӽڵ�ԭ���ĸ��ڵ�
//-Ȼ���������������л�����������ֵ,������ɺ��������ƽ��
//-���������ָ��ָ��,�ø��Ӱ�
//-����һ�����ģ������:
//-��������������,Ȼ��������������Ӷ���Ϊ��ͨ����(��ʵ���ǰ�����һ���ڵ�������)
//-����1 ������������
//-����2 �²���ڵ��үү�ڵ�
//-����3 
//-����4 ϵͳ��һ���������ṹ����ܼ�¼��������������,������Ǳ��
void TreeRotate(Tree* aTree, Node* curnode, int direction, int index)	//-��������תƽ�������
{
	Node* other = curnode->child[!direction];	//-ת������˵��ʵ���������ӽڵ�ĸ��ڵ㶨λ,��ôother���մ�ŵ�����ת��ĸ��ڵ�

	curnode->child[!direction] = other->child[direction];	//-����˸��ڵ��Ϻ��ڵ�ҽӵ�үү�ڵ���
	if (other->child[direction] != NULL)
		other->child[direction]->parent = curnode;
	other->parent = curnode->parent;	//-үү�ڵ�ĸ��ڵ�ָ����������ת�����нڵ�ĸ��ڵ�.����˵other�����ݴ�ľ�����ת��ĸ��ڵ�
	if (curnode->parent == NULL)
		aTree->index[index].root = other;	//-���үү�ڵ�û�и��ڵ�,��ô��ת��otherү��û�и��ڵ�,��ô�����Ǹ��ڵ�
	else if (curnode == curnode->parent->child[direction])	//-���үү�ڵ�ĸ��ڵ����,��ô������Ҫ����ָ����,���үү�ڵ㻻��
		curnode->parent->child[direction] = other;
	else
		curnode->parent->child[!direction] = other;
	other->child[direction] = curnode;	//-���ڵ�ĺ��ڵ�ָ����ԭ����үү�ڵ�,���������������и��ڵ���Ϣ�ı任
	curnode->parent = other;
}


Node* TreeBAASub(Tree* aTree, Node* curnode, int which, int index)
{
	Node* uncle = curnode->parent->parent->child[which];

	if (isRed(uncle))
	{
		curnode->parent->red = uncle->red = 0;
		curnode = curnode->parent->parent;
		curnode->red = 1;
	}
	else
	{
		if (curnode == curnode->parent->child[which])
		{
			curnode = curnode->parent;
			TreeRotate(aTree, curnode, !which, index);
		}
		curnode->parent->red = 0;
		curnode->parent->parent->red = 1;
		TreeRotate(aTree, curnode->parent->parent, which, index);	//-���ݹ�ȥ����үү�ڵ�
	}
	return curnode;
}


void TreeBalanceAfterAdd(Tree* aTree, Node* curnode, int index)	//-����������һ����,��Ҫ���ж�ƽ����
{
	while (curnode && isRed(curnode->parent) && curnode->parent->parent)	//-������Щ����˵�������������ͻ��Ҫ����
	{//-����Ҷ�ӽڵ�;���ڵ��Ǻ�ɫ��;ү�ڵ����
		if (curnode->parent == curnode->parent->parent->child[LEFT])	//-ͨ���Ƚ�ȡ������ڵ㼴�����ӽڵ�ĸ��ڵ���ֵܽڵ�
			curnode = TreeBAASub(aTree, curnode, RIGHT, index);
		else
			curnode = TreeBAASub(aTree, curnode, LEFT, index);
  }
  aTree->index[index].root->red = 0;	//-����2. ���ڵ��Ǻ�ɫ��
}


/**
 * Add an item to a tree
 * @param aTree the list to which the item is to be added
 * @param content the list item content itself
 * @param size the size of the element
 */
void* TreeAddByIndex(Tree* aTree, void* content, int size, int index)
{
	Node* curparent = NULL;
	Node* curnode = aTree->index[index].root;	//-��¼�˵�ǰ���ĸ��ڵ㣬�����Ŀ��Ӧ���ǴӸ��ڵ㿪ʼ����
	Node* newel = NULL;
	int left = 0;
	int result = 1;
	void* rc = NULL;

	while (curnode)	//-�ҵ���ͬ�Ļ�������һ���յĽڵ�,���ڴӸ��ڵ������ô���е�,���Զ��й���
	{
		result = aTree->index[index].compare(curnode->content, content, 1);	//-contentĿǰ���ݹ�������һ��ָ��ֵ,����ռ���洢���ǵ�ֵַ
		left = (result > 0);
		if (result == 0)
			break;	//-Ѱ�ҵ���ͬ���˳�
		else
		{
			curparent = curnode;	//-���ڵ�
			curnode = curnode->child[left];	//-�ֽڵ�
		}
	}
	
	if (result == 0)
	{//-�ҵ���һ����
		if (aTree->allow_duplicates)
			exit(-99);
		{
			newel = curnode;	//-�ҵ��˾ʹ���ԭ���Ľڵ�
			rc = newel->content;
			if (index == 0)
				aTree->size += (size - curnode->size);
		}
	}
	else
	{//-û���ҵ���ͬ�Ľڵ�,��ô������һ���ڵ�
		#if defined(UNIT_TESTS)
			newel = malloc(sizeof(Node));
		#else
			newel = (aTree->heap_tracking) ? mymalloc(__FILE__, __LINE__, sizeof(Node)) : malloc(sizeof(Node));	//-����һ���µĽڵ�,�����Ҫ�Ļ�����������Ϣ���и���
		#endif
		memset(newel, '\0', sizeof(Node));
		if (curparent)
			curparent->child[left] = newel;	//-������ڵ������˺��ʵĽڵ���
		else
			aTree->index[index].root = newel;	//-��������ڵ㶼û��,��ô���Լ����Ǹ��ڵ�
		newel->parent = curparent;
		newel->red = 1;
		if (index == 0)
		{
			++(aTree->count);	//-��������ɹ���������һ���½ڵ��,�����������ṹ���о�����һ��
			aTree->size += size;
		}
	}
	newel->content = content;	//-��ȷ�ϵĵ㸳ֵ
	newel->size = size;
	TreeBalanceAfterAdd(aTree, newel, index);	//-Ҳ��ֵ֮��ʹ�����ԭ����ƽ��,����Ϊ����������,���ٽ���ƽ��
	return rc;
}


void* TreeAdd(Tree* aTree, void* content, int size)	//-��tree�ṹ������һ����Ա���ڶ���������ʵ���ݾ���һ����ֵ��ֻ�������ֵ���Ա��ṹ��ʹ��
{
	void* rc = NULL;
	int i;

	for (i = 0; i < aTree->indexes; ++i)
		rc = TreeAddByIndex(aTree, content, size, i);	//-�����Ŀ¼Index,һ��Ӧ�ô���һ����

	return rc;
}

//-����Ӧ����������ԭ��
//-��Ŀ��ֵ(key),ȥ�͵�һ���ڵ�(root)�Ƚ�,�������еĽڵ㶼�ǰ��չ������е�,����
//-���Ժܿ��ҵ�(��ߵ�<�ұߵ�)
Node* TreeFindIndex1(Tree* aTree, void* key, int index, int value)	//-key��Ѱ�ҵ�Ŀ��
{
	int result = 0;
	Node* curnode = aTree->index[index].root;	//-��¼��������������Ϣ

	while (curnode)
	{
		result = aTree->index[index].compare(curnode->content, key, value);
		if (result == 0)
			break;	//-��Ⱦ��ҵ���
		else
			curnode = curnode->child[result > 0];	//-keyС�ڽڵ�ֵ,��ô��ȡ�ڵ���ߵ��ӽڵ�,����ȡ�ұߵ�
	}
	return curnode;
}


Node* TreeFindIndex(Tree* aTree, void* key, int index)
{
	return TreeFindIndex1(aTree, key, index, 0);
}


Node* TreeFindContentIndex(Tree* aTree, void* key, int index)
{
	return TreeFindIndex1(aTree, key, index, 1);
}


Node* TreeFind(Tree* aTree, void* key)
{
	return TreeFindIndex(aTree, key, 0);
}


Node* TreeMinimum(Node* curnode)
{
	if (curnode)
		while (curnode->child[LEFT])
			curnode = curnode->child[LEFT];
	return curnode;
}


Node* TreeSuccessor(Node* curnode)
{
	if (curnode->child[RIGHT])
		curnode = TreeMinimum(curnode->child[RIGHT]);
	else
	{
		Node* curparent = curnode->parent;
		while (curparent && curnode == curparent->child[RIGHT])
		{
			curnode = curparent;
			curparent = curparent->parent;
		}
		curnode = curparent;
	}
	return curnode;
}


Node* TreeNextElementIndex(Tree* aTree, Node* curnode, int index)
{
	if (curnode == NULL)
		curnode = TreeMinimum(aTree->index[index].root);
	else
		curnode = TreeSuccessor(curnode);
	return curnode;
}


Node* TreeNextElement(Tree* aTree, Node* curnode)
{
	return TreeNextElementIndex(aTree, curnode, 0);
}


Node* TreeBARSub(Tree* aTree, Node* curnode, int which, int index)
{
	Node* sibling = curnode->parent->child[which];

	if (isRed(sibling))
	{
		sibling->red = 0;
		curnode->parent->red = 1;
		TreeRotate(aTree, curnode->parent, !which, index);
		sibling = curnode->parent->child[which];
	}
	if (!sibling)
		curnode = curnode->parent;
	else if (isBlack(sibling->child[!which]) && isBlack(sibling->child[which]))
	{
		sibling->red = 1;
		curnode = curnode->parent;
	}
	else
	{
		if (isBlack(sibling->child[which]))
		{
			sibling->child[!which]->red = 0;
			sibling->red = 1;
			TreeRotate(aTree, sibling, which, index);
			sibling = curnode->parent->child[which];
		}
		sibling->red = curnode->parent->red;
		curnode->parent->red = 0;
		sibling->child[which]->red = 0;
		TreeRotate(aTree, curnode->parent, !which, index);
		curnode = aTree->index[index].root;
	}
	return curnode;
}


void TreeBalanceAfterRemove(Tree* aTree, Node* curnode, int index)
{
	while (curnode != aTree->index[index].root && isBlack(curnode))
	{
		/* curnode->content == NULL must equal curnode == NULL */
		if (((curnode->content) ? curnode : NULL) == curnode->parent->child[LEFT])
			curnode = TreeBARSub(aTree, curnode, RIGHT, index);
		else
			curnode = TreeBARSub(aTree, curnode, LEFT, index);
    }
	curnode->red = 0;
}


/**
 * Remove an item from a tree
 * @param aTree the list to which the item is to be added
 * @param curnode the list item content itself
 */
void* TreeRemoveNodeIndex(Tree* aTree, Node* curnode, int index)
{
	Node* redundant = curnode;
	Node* curchild = NULL;
	int size = curnode->size;
	void* content = curnode->content;

	/* if the node to remove has 0 or 1 children, it can be removed without involving another node */
	if (curnode->child[LEFT] && curnode->child[RIGHT]) /* 2 children */
		redundant = TreeSuccessor(curnode); 	/* now redundant must have at most one child */

	curchild = redundant->child[(redundant->child[LEFT] != NULL) ? LEFT : RIGHT];
	if (curchild) /* we could have no children at all */
		curchild->parent = redundant->parent;

	if (redundant->parent == NULL)
		aTree->index[index].root = curchild;
	else
	{
		if (redundant == redundant->parent->child[LEFT])
			redundant->parent->child[LEFT] = curchild;
		else
			redundant->parent->child[RIGHT] = curchild;
	}

	if (redundant != curnode)
	{
		curnode->content = redundant->content;
		curnode->size = redundant->size;
	}

	if (isBlack(redundant))
	{
		if (curchild == NULL)
		{
			if (redundant->parent)
			{
				Node temp;
				memset(&temp, '\0', sizeof(Node));
				temp.parent = (redundant) ? redundant->parent : NULL;
				temp.red = 0;
				TreeBalanceAfterRemove(aTree, &temp, index);
			}
		}
		else
			TreeBalanceAfterRemove(aTree, curchild, index);
	}

#if defined(UNIT_TESTS)
	free(redundant);
#else
	(aTree->heap_tracking) ? myfree(__FILE__, __LINE__, redundant) : free(redundant);
#endif
	if (index == 0)
	{
		aTree->size -= size;
		--(aTree->count);
	}
	return content;
}


/**
 * Remove an item from a tree
 * @param aTree the list to which the item is to be added
 * @param curnode the list item content itself
 */
void* TreeRemoveIndex(Tree* aTree, void* content, int index)
{
	Node* curnode = TreeFindContentIndex(aTree, content, index);

	if (curnode == NULL)
		return NULL;

	return TreeRemoveNodeIndex(aTree, curnode, index);
}


void* TreeRemove(Tree* aTree, void* content)
{
	int i;
	void* rc = NULL;

	for (i = 0; i < aTree->indexes; ++i)
		rc = TreeRemoveIndex(aTree, content, i);

	return rc;
}


void* TreeRemoveKeyIndex(Tree* aTree, void* key, int index)
{
	Node* curnode = TreeFindIndex(aTree, key, index);
	void* content = NULL;
	int i;

	if (curnode == NULL)
		return NULL;

	content = TreeRemoveNodeIndex(aTree, curnode, index);
	for (i = 0; i < aTree->indexes; ++i)
	{
		if (i != index)
			content = TreeRemoveIndex(aTree, content, i);
	}
	return content;
}


void* TreeRemoveKey(Tree* aTree, void* key)
{
	return TreeRemoveKeyIndex(aTree, key, 0);
}


int TreeIntCompare(void* a, void* b, int content)	//-�Ƚ�������С a > b����-1;a == b����0;a < b����1
{
	int i = *((int*)a);
	int j = *((int*)b);

	//printf("comparing %d %d\n", *((int*)a), *((int*)b));
	return (i > j) ? -1 : (i == j) ? 0 : 1;
}


int TreePtrCompare(void* a, void* b, int content)
{
	return (a > b) ? -1 : (a == b) ? 0 : 1;
}


int TreeStringCompare(void* a, void* b, int content)
{
	return strcmp((char*)a, (char*)b);
}

//-����û��,����Ҳ������һ����̷���
#if defined(UNIT_TESTS)

int check(Tree *t)
{
	Node* curnode = NULL;
	int rc = 0;

	curnode = TreeNextElement(t, curnode);
	while (curnode)
	{
		Node* prevnode = curnode;

		curnode = TreeNextElement(t, curnode);

		if (prevnode && curnode && (*(int*)(curnode->content) < *(int*)(prevnode->content)))
		{
			printf("out of order %d < %d\n", *(int*)(curnode->content), *(int*)(prevnode->content));
			rc = 99;
		}
	}
	return rc;
}


int traverse(Tree *t, int lookfor)
{
	Node* curnode = NULL;
	int rc = 0;

	printf("Traversing\n");
	curnode = TreeNextElement(t, curnode);
	//printf("content int %d\n", *(int*)(curnode->content));
	while (curnode)
	{
		Node* prevnode = curnode;

		curnode = TreeNextElement(t, curnode);
		//if (curnode)
		//	printf("content int %d\n", *(int*)(curnode->content));
		if (prevnode && curnode && (*(int*)(curnode->content) < *(int*)(prevnode->content)))
		{
			printf("out of order %d < %d\n", *(int*)(curnode->content), *(int*)(prevnode->content));
		}
		if (curnode && (lookfor == *(int*)(curnode->content)))
			printf("missing item %d actually found\n", lookfor);
	}
	printf("End traverse %d\n", rc);
	return rc;
}


int test(int limit)
{
	int i, *ip, *todelete;
	Node* current = NULL;
	Tree* t = TreeInitialize(TreeIntCompare);	//-������һ��tree�Ľṹ���Ա
	int rc = 0;

	printf("Tree initialized\n");

	srand(time(NULL));

	ip = malloc(sizeof(int));
	*ip = 2;
	TreeAdd(t, (void*)ip, sizeof(int));

	check(t);

	i = 2;
	void* result = TreeRemove(t, (void*)&i);
	if (result)
		free(result);

	int actual[limit];
	for (i = 0; i < limit; i++)
	{
		void* replaced = NULL;

		ip = malloc(sizeof(int));
		*ip = rand();
		replaced = TreeAdd(t, (void*)ip, sizeof(int));
		if (replaced) /* duplicate */
		{
			free(replaced);
			actual[i] = -1;
		}
		else
			actual[i] = *ip;
		if (i==5)
			todelete = ip;
		printf("Tree element added %d\n",  *ip);
		if (1 % 1000 == 0)
		{
			rc = check(t);
			printf("%d elements, check result %d\n", i+1, rc);
			if (rc != 0)
				return 88;
		}
	}

	check(t);

	for (i = 0; i < limit; i++)
	{
		int parm = actual[i];

		if (parm == -1)
			continue;

		Node* found = TreeFind(t, (void*)&parm);
		if (found)
			printf("Tree find %d %d\n", parm, *(int*)(found->content));
		else
		{
			printf("%d not found\n", parm);
			traverse(t, parm);
			return -2;
		}
	}

	check(t);

	for (i = limit -1; i >= 0; i--)
	{
		int parm = actual[i];
		void *found;

		if (parm == -1) /* skip duplicate */
			continue;

		found = TreeRemove(t, (void*)&parm);
		if (found)
		{
			printf("%d Tree remove %d %d\n", i, parm, *(int*)(found));
			free(found);
		}
		else
		{
			int count = 0;
			printf("%d %d not found\n", i, parm);
			traverse(t, parm);
			for (i = 0; i < limit; i++)
				if (actual[i] == parm)
					++count;
			printf("%d occurs %d times\n", parm, count);
			return -2;
		}
		if (i % 1000 == 0)
		{
			rc = check(t);
			printf("%d elements, check result %d\n", i+1, rc);
			if (rc != 0)
				return 88;
		}
	}
	printf("finished\n");
	return 0;
}

int main(int argc, char *argv[])
{
	int rc = 0;

	while (rc == 0)
		rc = test(999999);
}

#endif





