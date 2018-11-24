#ifndef STRINGS_H
#define STRINGS_H
#include <stddef.h>
void int_to_ascii(int n, char str[]);
void reverse(char s[]);
int strlen(char s[]);
void backspace(char s[]);
void append(char s[], char n);
int strcmp(char s1[], char s2[]);
void memcpy(void *dest, void *src, size_t n);
#endif