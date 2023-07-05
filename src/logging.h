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
#include <stdlib.h>
__attribute__((unused)) static int get_gs_log_level() {
  static bool gs_log_level_init = false;
  static int gs_log_level_level = 0;
  if (!gs_log_level_init) {
    char *ll = getenv("LOG_LEVEL");
    gs_log_level_level = ll ? atoi(ll) : LVLNO_INFO;
  }
  return gs_log_level_level;
}
#  define LOG_LEVEL (get_gs_log_level())
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

#define LOG_COLOUR_OF(LEVEL) LOG_COLOUR_##LEVEL
#define LOG_IF_ENABLED(LEVEL, ...) do { if (LOG_ENABLED_##LEVEL) __VA_ARGS__ } while(0)
#define LOG(LEVEL, M, ...) do { LOG_IF_ENABLED(LEVEL, fprintf(stderr, "" LOG_COLOUR_OF(LEVEL) "["#LEVEL"] %s (%s:%d) " M NONE "\n", __func__, FILENAME, __LINE__, __VA_ARGS__); ); } while(0)

#define LOG_ENABLED_FATAL (LVLNO_FATAL <= LOG_LEVEL)
#define LOG_ENABLED_ERROR (LVLNO_ERROR <= LOG_LEVEL)
#define LOG_ENABLED_WARN (LVLNO_WARN <= LOG_LEVEL)
#define LOG_ENABLED_INFO (LVLNO_INFO <= LOG_LEVEL)
#define LOG_ENABLED_DEBUG (LVLNO_DEBUG <= LOG_LEVEL)
#define LOG_ENABLED_TRACE (LVLNO_TRACE <= LOG_LEVEL)

#define LOG_COLOUR_FATAL RED
#define LOG_COLOUR_ERROR BROWN
#define LOG_COLOUR_WARN YELLOW
#define LOG_COLOUR_INFO GREEN
#define LOG_COLOUR_DEBUG PURPLE
#define LOG_COLOUR_TRACE L_BLACK

#define LOG_FATAL(M, ...) LOG(FATAL, M, __VA_ARGS__)
#define LOG_ERROR(M, ...) LOG(ERROR, M, __VA_ARGS__)
#define LOG_WARN(M, ...) LOG(WARN, M, __VA_ARGS__)
#define LOG_INFO(M, ...) LOG(INFO, M, __VA_ARGS__)
#define LOG_DEBUG(M, ...) LOG(DEBUG, M, __VA_ARGS__)
#define LOG_TRACE(M, ...) LOG(TRACE, M, __VA_ARGS__)
