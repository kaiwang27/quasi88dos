#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

/* Initial Open Watcom/CauseWay target. See dos/README.md for current scope. */
#if !defined(__WATCOMC__) || !defined(__386__) || !defined(__DOS__)
#error This backend requires Open Watcom targeting 32-bit DOS
#endif
#define QUASI88_DOS
#define LSB_FIRST
#define Q_COMMENT "DOS port (bring-up)"
#define INLINE static __inline
/* Memory-only renderer for machine bring-up; no hardware display yet. */
#define SUPPORT_16BPP
#define HAVE_INTPTR_T
#define CLIB_DECL

#endif
