#ifndef UUID_0196293C_0C80_77FA_9D89_921A0FF9BA1D
#define UUID_0196293C_0C80_77FA_9D89_921A0FF9BA1D

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern void (*outputChar)(int);

#define UNITY_EXCLUDE_SETJMP_H
#define UNITY_EXCLUDE_MATH_H
#define UNITY_EXCLUDE_STDDEF_H
#define UNITY_EXCLUDE_STDINT_H
#define UNITY_EXCLUDE_LIMITS_H
#define UNITY_OUTPUT_CHAR(c) (void)outputChar(c)
#define UNITY_OUTPUT_FLUSH() (void)(0)
#define UNITY_EXCLUDE_FLOAT
#define UNITY_EXCLUDE_FLOAT_PRINT
#ifndef NULL
#define NULL (void *)0
#endif /* NULL */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* UUID_0196293C_0C80_77FA_9D89_921A0FF9BA1D */
