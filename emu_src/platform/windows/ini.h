#ifndef INI_H
#define INI_H

// C style naming conventions to be compatible with old allegro code
// NOT unicode compatible
#include <stdbool.h>

//Be sure to include the \\ when opening a file or THIS CODE doesn't work!!!!!

#ifdef __cplusplus
extern "C" {
#endif

void set_config_file(const char *szFileName);

// Get
int get_config_int(const char *szSection, const char *szKey, int iDefaultValue);
float get_config_float(const char *szSection, const char *szKey, float fltDefaultValue);
bool get_config_bool(const char *szSection, const char *szKey, bool bolDefaultValue);
// Returns a malloc'd string the caller must free(), or NULL on failure.
char *get_config_string(const char *szSection, const char *szKey, const char *szDefaultValue);

// Set
void set_config_int(const char *szSection, const char *szKey, int iValue);
void set_config_float(const char *szSection, const char *szKey, float fltValue);
void set_config_bool(const char *szSection, const char *szKey, bool bolValue);
void set_config_string(const char *szSection, const char *szKey, const char *szValue);


#ifdef __cplusplus
}
#endif


#endif//INI_H
