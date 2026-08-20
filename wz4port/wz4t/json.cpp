/****************************************************************************/
/***                                                                      ***/
/***   A small JSON reader                                                ***/
/***                                                                      ***/
/****************************************************************************/

#include "json.hpp"
#include "base/system.hpp"

#include <stdio.h>
#include <stdlib.h>

/****************************************************************************/

void wFormatFloat(sChar *buffer,sInt size,sF32 value)
{
  char narrow[64];
  narrow[0] = 0;
  for(int prec=6;prec<=9;prec++)
  {
    snprintf(narrow,sizeof(narrow),"%.*g",prec,double(value));
    if(strtof(narrow,0)==value)
      break;
  }

  sInt i = 0;
  for(const char *s=narrow;*s && i<size-1;s++)
    buffer[i++] = sChar(*s);
  buffer[i] = 0;
}

sF32 wParseFloat(const sChar *text)
{
  char narrow[128];
  sInt i = 0;
  for(;text[i] && i<sInt(sizeof(narrow))-1;i++)
    narrow[i] = char(text[i]);
  narrow[i] = 0;
  return strtof(narrow,0);
}

/****************************************************************************/

wJsonValue::wJsonValue()
{
  Type = wJSON_NULL;
  Bool = 0;
  Number = 0;
}

const wJsonValue *wJsonValue::Member(const sChar *key) const
{
  if(Type!=wJSON_OBJECT)
    return 0;
  const wJsonMember *m;
  sFORALL(Members,m)
    if(m->Key==key)
      return m->Value;
  return 0;
}

sInt wJsonValue::GetInt(const sChar *key,sInt def) const
{
  const wJsonValue *v = Member(key);
  return (v && v->Type==wJSON_NUMBER) ? sInt(v->Number) : def;
}

sF32 wJsonValue::GetFloat(const sChar *key,sF32 def) const
{
  const wJsonValue *v = Member(key);
  return (v && v->Type==wJSON_NUMBER) ? sF32(v->Number) : def;
}

sBool wJsonValue::GetBool(const sChar *key,sBool def) const
{
  const wJsonValue *v = Member(key);
  return (v && v->Type==wJSON_BOOL) ? v->Bool : def;
}

sPoolString wJsonValue::GetString(const sChar *key,const sChar *def) const
{
  const wJsonValue *v = Member(key);
  return (v && v->Type==wJSON_STRING) ? v->String : sPoolString(def);
}

const wJsonValue *wJsonValue::GetArray(const sChar *key) const
{
  const wJsonValue *v = Member(key);
  return (v && v->Type==wJSON_ARRAY) ? v : 0;
}

/****************************************************************************/

wJsonDoc::wJsonDoc()
{
  Scan = 0;
  Start = 0;
  Line = 1;
  Failed = 0;
  Root = 0;
  FileText = 0;
  ErrorMsg = L"";
}

wJsonDoc::~wJsonDoc()
{
  wJsonValue *v;
  sFORALL(All,v)
    delete v;
  delete[] FileText;
}

void wJsonDoc::Error(const sChar *what)
{
  if(Failed)                      // keep the first error, it is the useful one
    return;
  Failed = 1;
  ErrorMsg.PrintF(L"line %d: %s",Line,what);
}

wJsonValue *wJsonDoc::New(sInt type)
{
  wJsonValue *v = new wJsonValue;
  v->Type = type;
  All.AddTail(v);
  return v;
}

void wJsonDoc::Space()
{
  for(;;)
  {
    if(*Scan=='\n')
    {
      Line++;
      Scan++;
    }
    else if(*Scan==' ' || *Scan=='\t' || *Scan=='\r')
    {
      Scan++;
    }
    else
    {
      break;
    }
  }
}

sBool wJsonDoc::String(sPoolString &out)
{
  if(*Scan!='"')
  {
    Error(L"string expected");
    return 0;
  }
  Scan++;

  sTextBuffer tb;
  while(*Scan && *Scan!='"')
  {
    if(*Scan=='\\')
    {
      Scan++;
      switch(*Scan)
      {
      case '"':  tb.PrintChar('"');  Scan++; break;
      case '\\': tb.PrintChar('\\'); Scan++; break;
      case '/':  tb.PrintChar('/');  Scan++; break;
      case 'b':  tb.PrintChar('\b'); Scan++; break;
      case 'f':  tb.PrintChar('\f'); Scan++; break;
      case 'n':  tb.PrintChar('\n'); Scan++; break;
      case 'r':  tb.PrintChar('\r'); Scan++; break;
      case 't':  tb.PrintChar('\t'); Scan++; break;
      case 'u':
        {
          Scan++;
          sInt code = 0;
          for(sInt i=0;i<4;i++)
          {
            sInt digit = -1;
            if(*Scan>='0' && *Scan<='9') digit = *Scan-'0';
            if(*Scan>='a' && *Scan<='f') digit = *Scan-'a'+10;
            if(*Scan>='A' && *Scan<='F') digit = *Scan-'A'+10;
            if(digit<0)
            {
              Error(L"bad \\u escape");
              return 0;
            }
            code = code*16 + digit;
            Scan++;
          }
          // sChar is 2 bytes here, so a BMP code point is one unit and a
          // surrogate pair arrives as two \u escapes that both pass through
          // unchanged — which is what a UTF-16 consumer wants.
          tb.PrintChar(sChar(code));
        }
        break;
      default:
        Error(L"bad escape");
        return 0;
      }
    }
    else
    {
      if(*Scan=='\n')
        Line++;
      tb.PrintChar(*Scan);
      Scan++;
    }
  }

  if(*Scan!='"')
  {
    Error(L"unterminated string");
    return 0;
  }
  Scan++;

  out = sPoolString(tb.Get());
  return 1;
}

wJsonValue *wJsonDoc::Value()
{
  Space();
  if(Failed)
    return 0;

  switch(*Scan)
  {
  case '{':
    {
      Scan++;
      wJsonValue *v = New(wJSON_OBJECT);
      Space();
      if(*Scan=='}')
      {
        Scan++;
        return v;
      }
      for(;;)
      {
        Space();
        wJsonMember *m = v->Members.AddMany(1);
        m->Value = 0;
        if(!String(m->Key))
          return 0;
        Space();
        if(*Scan!=':')
        {
          Error(L"':' expected");
          return 0;
        }
        Scan++;
        m->Value = Value();
        if(!m->Value)
          return 0;
        Space();
        if(*Scan==',')
        {
          Scan++;
          continue;
        }
        if(*Scan=='}')
        {
          Scan++;
          return v;
        }
        Error(L"',' or '}' expected");
        return 0;
      }
    }

  case '[':
    {
      Scan++;
      wJsonValue *v = New(wJSON_ARRAY);
      Space();
      if(*Scan==']')
      {
        Scan++;
        return v;
      }
      for(;;)
      {
        wJsonValue *item = Value();
        if(!item)
          return 0;
        v->Items.AddTail(item);
        Space();
        if(*Scan==',')
        {
          Scan++;
          continue;
        }
        if(*Scan==']')
        {
          Scan++;
          return v;
        }
        Error(L"',' or ']' expected");
        return 0;
      }
    }

  case '"':
    {
      wJsonValue *v = New(wJSON_STRING);
      if(!String(v->String))
        return 0;
      return v;
    }

  case 't':
    if(sCmpStringLen(Scan,L"true",4)==0)
    {
      Scan += 4;
      wJsonValue *v = New(wJSON_BOOL);
      v->Bool = 1;
      return v;
    }
    Error(L"'true' expected");
    return 0;

  case 'f':
    if(sCmpStringLen(Scan,L"false",5)==0)
    {
      Scan += 5;
      wJsonValue *v = New(wJSON_BOOL);
      v->Bool = 0;
      return v;
    }
    Error(L"'false' expected");
    return 0;

  case 'n':
    if(sCmpStringLen(Scan,L"null",4)==0)
    {
      Scan += 4;
      return New(wJSON_NULL);
    }
    Error(L"'null' expected");
    return 0;

  default:
    {
      // Number. Hand the span to strtod rather than reimplementing exponent
      // and rounding rules — the same reasoning as opsmeta's writer, which
      // does not trust Altona's float formatting either.
      const sChar *begin = Scan;
      if(*Scan=='-' || *Scan=='+')
        Scan++;
      sBool any = 0;
      while((*Scan>='0' && *Scan<='9') || *Scan=='.')
      {
        any = 1;
        Scan++;
      }
      if(*Scan=='e' || *Scan=='E')
      {
        Scan++;
        if(*Scan=='-' || *Scan=='+')
          Scan++;
        while(*Scan>='0' && *Scan<='9')
          Scan++;
      }
      if(!any)
      {
        Error(L"value expected");
        return 0;
      }

      char narrow[64];
      sInt len = sInt(Scan-begin);
      if(len>=sInt(sizeof(narrow)))
        len = sizeof(narrow)-1;
      for(sInt i=0;i<len;i++)
        narrow[i] = char(begin[i]);
      narrow[len] = 0;

      wJsonValue *v = New(wJSON_NUMBER);
      v->Number = strtod(narrow,0);
      return v;
    }
  }
}

wJsonValue *wJsonDoc::Parse(const sChar *text)
{
  Start = text;
  Scan = text;
  Line = 1;
  Failed = 0;
  Root = 0;

  Root = Value();
  if(Root)
  {
    Space();
    if(*Scan!=0)
    {
      Error(L"trailing data after the top-level value");
      Root = 0;
    }
  }
  return Root;
}

wJsonValue *wJsonDoc::Load(const sChar *filename)
{
  delete[] FileText;
  FileText = sLoadText(filename);
  if(!FileText)
  {
    ErrorMsg.PrintF(L"could not read <%s>",filename);
    Failed = 1;
    return 0;
  }
  return Parse(FileText);
}

/****************************************************************************/
