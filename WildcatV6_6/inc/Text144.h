#ifndef WILDCAT_TEXT144_H
#define WILDCAT_TEXT144_H
#define TEXT144_COLUMNS 48u
/* At x=8, 48 ordinary 8px glyphs fit the 400px LCD. */
static inline void Text144_Row(char output[TEXT144_COLUMNS+1], const char *text)
{
    unsigned i=0;
    if (text) while (i<TEXT144_COLUMNS && *text) output[i++]=*text++;
    while (i<TEXT144_COLUMNS) output[i++]=' ';
    output[TEXT144_COLUMNS]=0;
}
#endif
