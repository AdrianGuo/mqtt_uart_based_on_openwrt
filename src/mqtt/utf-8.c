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
 *    Ian Craggs - initial API and implementation and/or initial documentation
 *******************************************************************************/


/**
 * @file
 * \brief Functions for checking that strings contain UTF-8 characters only
 *
 * See page 104 of the Unicode Standard 5.0 for the list of well formed
 * UTF-8 byte sequences.
 * 
 */

#include <stdlib.h>
#include <string.h>

#include "StackTrace.h"

/**
 * Macro to determine the number of elements in a single-dimension array
 */
#if !defined(ARRAY_SIZE)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

/*
万国码字符说明:
实际表示ASCII字符的UNICODE字符，将会编码成1个字节，并且UTF-8表示与ASCII字符表示
是一样的。所有其他的UNICODE字符转化成UTF-8将需要至少2个字节。每个字节由一个换码
序列开始。第一个字节由唯一的换码序列，由n位连续的1加一位0组成, 首字节连续的1的
个数表示字符编码所需的字节数。
Unicode转换为UTF-8时，可以将Unicode二进制从低位往高位取出二进制数字，每次取6位，
如上述的二进制就可以分别取出为如下示例所示的格式，前面按格式填补，不足8位用0填补。
注：Unicode转换为UTF-8需要的字节数可以根据这个规则计算：如果Unicode小于0X80（Ascii字符），
则转换后为1个字节。否则转换后的字节数为Unicode二进制位数减1再除以5。
示例
UNICODE uCA(1100 1010) 编码成UTF-8将需要2个字节：
uCA -> C3 8A， 过程如下：
uCA(1100 1010)处于0080 ~07FF之间，从上文中的转换表可知对其编码需要2bytes，即两个
字节，其对 应 UTF-8格式为： 110X XXXX10XX XXXX。从此格式中可以看到，对其编码还需
要11位，而uCA(1100 1010)仅有8位，这时需要在其二进制数前补0凑成11位: 000 1100 1010,
依次填入110X XXXX 10XX XXXX的空位中， 即得 1100 0011 1000 1010（C38A）。
其实就是把不同长度的字节,用一种可阅读的协议表示出来
*/

/**
 * Structure to hold the valid ranges of UTF-8 characters, for each byte up to 4
 */
struct
{
	int len; /**< number of elements in the following array (1 to 4) */
	struct
	{
		char lower; /**< lower limit of valid range */
		char upper; /**< upper limit of valid range */
	} bytes[4];   /**< up to 4 bytes can be used per character */
}
valid_ranges[] = 
{
		{1, { {00, 0x7F} } },
		{2, { {0xC2, 0xDF}, {0x80, 0xBF} } },
		{3, { {0xE0, 0xE0}, {0xA0, 0xBF}, {0x80, 0xBF} } },
		{3, { {0xE1, 0xEC}, {0x80, 0xBF}, {0x80, 0xBF} } },
		{3, { {0xED, 0xED}, {0x80, 0x9F}, {0x80, 0xBF} } },
		{3, { {0xEE, 0xEF}, {0x80, 0xBF}, {0x80, 0xBF} } },
		{4, { {0xF0, 0xF0}, {0x90, 0xBF}, {0x80, 0xBF}, {0x80, 0xBF} } },
		{4, { {0xF1, 0xF3}, {0x80, 0xBF}, {0x80, 0xBF}, {0x80, 0xBF} } },
		{4, { {0xF4, 0xF4}, {0x80, 0x8F}, {0x80, 0xBF}, {0x80, 0xBF} } },
};


/**
 * Validate a single UTF-8 character
 * @param len the length of the string in "data"
 * @param data the bytes to check for a valid UTF-8 char
 * @return pointer to the start of the next UTF-8 character in "data"
 */
const char* UTF8_char_validate(int len, const char* data)	//-根据格式判断是否是这个类型的字符
{
	int good = 0;
	int charlen = 2;
	int i, j;
	const char *rc = NULL;
	//-UTF-8用1到4个字节编码Unicode字符。
	FUNC_ENTRY;
	/* first work out how many bytes this char is encoded in */
	if ((data[0] & 128) == 0)	//-等于0肯定就是一个字节
		charlen = 1;
	else if ((data[0] & 0xF0) == 0xF0)	//-就是一套协议
		charlen = 4;
	else if ((data[0] & 0xE0) == 0xE0)
		charlen = 3;

	if (charlen > len)
		goto exit;	/* not enough characters in the string we were given */

	for (i = 0; i < ARRAY_SIZE(valid_ranges); ++i)
	{ /* just has to match one of these rows */
		if (valid_ranges[i].len == charlen)
		{
			good = 1;
			for (j = 0; j < charlen; ++j)
			{
				if (data[j] < valid_ranges[i].bytes[j].lower ||
						data[j] > valid_ranges[i].bytes[j].upper)
				{
					good = 0;  /* failed the check */
					break;
				}
			}
			if (good)
				break;
		}
	}

	if (good)
		rc = data + charlen;
	exit:
	FUNC_EXIT;
	return rc;
}


/**
 * Validate a length-delimited string has only UTF-8 characters
 * @param len the length of the string in "data"
 * @param data the bytes to check for valid UTF-8 characters
 * @return 1 (true) if the string has only UTF-8 characters, 0 (false) otherwise
 */
int UTF8_validate(int len, const char* data)	//-这个作为一个完整的函数块没有必要去深究知道功能(返回1说明仅有UTF-8字符,否则不是)即可,然后有地方需要的可以来移植
{
	const char* curdata = NULL;
	int rc = 0;

	FUNC_ENTRY;
	if (len == 0)
	{
		rc = 1;
		goto exit;
	}
	curdata = UTF8_char_validate(len, data);
	while (curdata && (curdata < data + len))
		curdata = UTF8_char_validate(len, curdata);	//-如果识别符合的话,就继续下一个,直到最后或遇到不符合的

	rc = curdata != NULL;
exit:
	FUNC_EXIT_RC(rc);
	return rc;
}


/**
 * Validate a null-terminated string has only UTF-8 characters
 * @param string the string to check for valid UTF-8 characters
 * @return 1 (true) if the string has only UTF-8 characters, 0 (false) otherwise
 */
int UTF8_validateString(const char* string)	//-判断是否仅仅包含UTF-8 characters
{
	int rc = 0;

	FUNC_ENTRY;		//-这句话的函数功能肯定是要理解的,但是现在对于协议是没有关系的,先方法
	rc = UTF8_validate(strlen(string), string);
	FUNC_EXIT_RC(rc);
	return rc;
}



#if defined(UNIT_TESTS)
#include <stdio.h>

typedef struct
{
	int len;
	char data[20];
} tests;

tests valid_strings[] =
{
		{3, "hjk" },
		{7, {0x41, 0xE2, 0x89, 0xA2, 0xCE, 0x91, 0x2E} },
		{3, {'f', 0xC9, 0xB1 } },
		{9, {0xED, 0x95, 0x9C, 0xEA, 0xB5, 0xAD, 0xEC, 0x96, 0xB4} },
		{9, {0xE6, 0x97, 0xA5, 0xE6, 0x9C, 0xAC, 0xE8, 0xAA, 0x9E} },
		{4, {0x2F, 0x2E, 0x2E, 0x2F} },
		{7, {0xEF, 0xBB, 0xBF, 0xF0, 0xA3, 0x8E, 0xB4} },
};

tests invalid_strings[] =
{
		{2, {0xC0, 0x80} },
		{5, {0x2F, 0xC0, 0xAE, 0x2E, 0x2F} },
		{6, {0xED, 0xA1, 0x8C, 0xED, 0xBE, 0xB4} },
		{1, {0xF4} },
};

int main (int argc, char *argv[])
{
	int i, failed = 0;

	for (i = 0; i < ARRAY_SIZE(valid_strings); ++i)
	{
		if (!UTF8_validate(valid_strings[i].len, valid_strings[i].data))
		{
			printf("valid test %d failed\n", i);
			failed = 1;
		}
		else
			printf("valid test %d passed\n", i);
	}

	for (i = 0; i < ARRAY_SIZE(invalid_strings); ++i)
	{
		if (UTF8_validate(invalid_strings[i].len, invalid_strings[i].data))
		{
			printf("invalid test %d failed\n", i);
			failed = 1;
		}
		else
			printf("invalid test %d passed\n", i);
	}

	if (failed)
		printf("Failed\n");
	else
		printf("Passed\n");

	return 0;
} /* End of main function*/

#endif

