#ifndef _USR_IME_DICTIONARY_H
#define _USR_IME_DICTIONARY_H

int ime_dictionary_lookup(const char *reading,
                          const char *const **out_candidates,
                          int *out_count);

#endif /* _USR_IME_DICTIONARY_H */
