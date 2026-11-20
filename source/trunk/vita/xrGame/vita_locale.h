#pragma once
inline unsigned vita_language=0;
inline const char* vita_text(const char* french,const char* english){return vita_language?english:french;}
