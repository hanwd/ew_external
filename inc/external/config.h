#ifndef __H_EWA_EXTERNAL_CONFIG__
#define __H_EWA_EXTERNAL_CONFIG__


#ifdef EWA_EXTERNAL_DLL
#ifdef _MSC_VER
#define EWA_EXTERNAL_EXPORT __declspec(dllexport)
#define EWA_EXTERNAL_IMPORT __declspec(dllimport)
#else
#define EWA_EXTERNAL_EXPORT __attribute__((dllexport))
#define EWA_EXTERNAL_IMPORT __attribute__((dllimport))
#endif
#else
#define EWA_EXTERNAL_EXPORT
#define EWA_EXTERNAL_IMPORT
#endif

#ifdef EWA_EXTERNAL_BUILDING
#define DLLIMPEXP_EWA_EXTERNAL EWA_EXTERNAL_EXPORT
#else
#define DLLIMPEXP_EWA_EXTERNAL EWA_EXTERNAL_IMPORT
#endif

#endif
