#pragma once

/**
 * Inspired and partially taken from lwlog:
 * https://github.com/Akagi201/lwlog/blob/b93fb390a0535edaa53d6b8d72c4e2768b272b30/lwlog.h
 * (which is under MIT license)
 */

#include <stdio.h>
#include <string.h>

// log levels from log4j
#define LVLNO_NONE 0
#define LVLNO_FATAL 1
#define LVLNO_ERROR 2
#define LVLNO_WARN 3
#define LVLNO_INFO 4
#define LVLNO_DEBUG 5
#define LVLNO_TRACE 6

#ifndef LOG_LEVEL
#  define LOG_LEVEL LVLNO_DEBUG
#endif

#ifndef LOG_COLOUR
#  define LOG_COLOUR 1
#endif

#if LOG_COLOUR < 1
#  define IF_COLOUR(_X) ""
#else
#  define IF_COLOUR(X) X
#endif

// colours
#define NONE      IF_COLOUR("\e[0m")
#define BLACK     IF_COLOUR("\e[0;30m")
#define L_BLACK   IF_COLOUR("\e[1;30m")
#define RED       IF_COLOUR("\e[0;31m")
#define L_RED     IF_COLOUR("\e[1;31m")
#define GREEN     IF_COLOUR("\e[0;32m")
#define L_GREEN   IF_COLOUR("\e[1;32m")
#define BROWN     IF_COLOUR("\e[0;33m")
#define YELLOW    IF_COLOUR("\e[1;33m")
#define BLUE      IF_COLOUR("\e[0;34m")
#define L_BLUE    IF_COLOUR("\e[1;34m")
#define PURPLE    IF_COLOUR("\e[0;35m")
#define L_PURPLE  IF_COLOUR("\e[1;35m")
#define CYAN      IF_COLOUR("\e[0;36m")
#define L_CYAN    IF_COLOUR("\e[1;36m")
#define GRAY      IF_COLOUR("\e[0;37m")
#define WHITE     IF_COLOUR("\e[1;37m")

#define BOLD      IF_COLOUR("\e[1m")
#define UNDERLINE IF_COLOUR("\e[4m")
#define BLINK     IF_COLOUR("\e[5m")
#define REVERSE   IF_COLOUR("\e[7m")
#define HIDE      IF_COLOUR("\e[8m")
#define CLEAR     IF_COLOUR("\e[2J")
#define CLRLINE   IF_COLOUR("\r\e[K") // or "\e[1K\r"

#define FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define LOG_FATAL(M, ...) do { fprintf(stderr, RED     "[FATAL] " "%s (%s:%d) " M NONE "\n", __func__, FILENAME, __LINE__, __VA_ARGS__); } while(0)
#define LOG_ERROR(M, ...) do { fprintf(stderr, BROWN   "[ERROR] " "%s (%s:%d) " M NONE "\n", __func__, FILENAME, __LINE__, __VA_ARGS__); } while(0)
#define LOG_WARN(M, ...)  do { fprintf(stderr, YELLOW  "[WARN]  " "%s (%s:%d) " M NONE "\n", __func__, FILENAME, __LINE__, __VA_ARGS__); } while(0)
#define LOG_INFO(M, ...)  do { fprintf(stderr, GREEN   "[INFO]  " "%s (%s:%d) " M NONE "\n", __func__, FILENAME, __LINE__, __VA_ARGS__); } while(0)
#define LOG_DEBUG(M, ...) do { fprintf(stderr, PURPLE  "[DEBUG] " "%s (%s:%d) " M NONE "\n", __func__, FILENAME, __LINE__, __VA_ARGS__); } while(0)
#define LOG_TRACE(M, ...) do { fprintf(stderr, L_BLACK "[TRACE] " "%s (%s:%d) " M NONE "\n", __func__, FILENAME, __LINE__, __VA_ARGS__); } while(0)

// LOG_LEVEL controls
#if LOG_LEVEL < LVLNO_FATAL
#  undef LOG_FATAL
#  define LOG_FATAL(M, ...) while(0)
#endif
#if LOG_LEVEL < LVLNO_ERROR
#  undef LOG_ERROR
#  define LOG_ERROR(M, ...) while(0)
#endif
#if LOG_LEVEL < LVLNO_WARN
#  undef LOG_WARN
#  define LOG_WARN(M, ...) while(0)
#endif
#if LOG_LEVEL < LVLNO_INFO
#  undef LOG_INFO
#  define LOG_INFO(M, ...) while(0)
#endif
#if LOG_LEVEL < LVLNO_DEBUG
#  undef LOG_DEBUG
#  define LOG_DEBUG(M, ...) while(0)
#endif
#if LOG_LEVEL < LVLNO_TRACE
#  undef LOG_TRACE
#  define LOG_TRACE(M, ...) while(0)
#endif

