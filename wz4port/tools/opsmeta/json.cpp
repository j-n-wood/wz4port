/****************************************************************************/
/***                                                                      ***/
/***   A very small JSON writer                                           ***/
/***                                                                      ***/
/****************************************************************************/

#include "json.hpp"

#include <stdio.h>
#include <stdlib.h>

/****************************************************************************/

wJsonWriter::wJsonWriter(sTextBuffer &out)
{
  Out = &out;
}

void wJsonWriter::Indent()
{
  sInt depth = Empty.GetCount();
  for(sInt i=0;i<depth;i++)
    Out->Print(L"  ");
}

void wJsonWriter::Comma()
{
  sInt depth = Empty.GetCount();
  if(depth>0)
  {
    if(!Empty[depth-1])
      Out->Print(L",");
    Out->Print(L"\n");
    Empty[depth-1] = 0;
  }
  Indent();
}

void wJsonWriter::Key(const sChar *key)
{
  if(key)
  {
    Escaped(key);
    Out->Print(L": ");
  }
}

void wJsonWriter::Escaped(const sChar *s)
{
  Out->Print(L"\"");
  for(;*s;s++)
  {
    sChar c = *s;
    switch(c)
    {
    case '"':  Out->Print(L"\\\""); break;
    case '\\': Out->Print(L"\\\\"); break;
    case '\b': Out->Print(L"\\b");  break;
    case '\f': Out->Print(L"\\f");  break;
    case '\n': Out->Print(L"\\n");  break;
    case '\r': Out->Print(L"\\r");  break;
    case '\t': Out->Print(L"\\t");  break;
    default:
      if(c>=0x20 && c<0x7f)
        Out->PrintChar(c);
      else
        Out->PrintF(L"\\u%04x",sInt(sU16(c)));
      break;
    }
  }
  Out->Print(L"\"");
}

/****************************************************************************/

void wJsonWriter::BeginObject(const sChar *key)
{
  Comma();
  Key(key);
  Out->Print(L"{");
  *Empty.AddMany(1) = 1;
}

void wJsonWriter::EndObject()
{
  sVERIFY(Empty.GetCount()>0);
  sInt empty = Empty.RemTail();
  if(!empty)
  {
    Out->Print(L"\n");
    Indent();
  }
  Out->Print(L"}");
}

void wJsonWriter::BeginArray(const sChar *key)
{
  Comma();
  Key(key);
  Out->Print(L"[");
  *Empty.AddMany(1) = 1;
}

void wJsonWriter::EndArray()
{
  sVERIFY(Empty.GetCount()>0);
  sInt empty = Empty.RemTail();
  if(!empty)
  {
    Out->Print(L"\n");
    Indent();
  }
  Out->Print(L"]");
}

/****************************************************************************/

void wJsonWriter::Str(const sChar *key,const sChar *value)
{
  Comma();
  Key(key);
  Escaped(value ? value : L"");
}

void wJsonWriter::Int(const sChar *key,sInt value)
{
  Comma();
  Key(key);
  Out->PrintF(L"%d",value);
}

void wJsonWriter::Bool(const sChar *key,sBool value)
{
  Comma();
  Key(key);
  Out->Print(value ? L"true" : L"false");
}

void wJsonWriter::HexStr(const sChar *key,sU32 value)
{
  Comma();
  Key(key);
  Out->PrintF(L"\"0x%08x\"",value);
}

// Floats go through libc, NOT through sTextBuffer::PrintF.
//
// Altona's own float formatter is not correctly rounded: asked for nine
// decimals it renders 4.0f as "4.00000023" and 0.125f as "0.125000007", both of
// which are exactly representable and should print exactly. Those digits are
// noise, and noise in a file whose whole purpose is to be reviewed by hand and
// diffed is worse than useless.
//
// So: print with the shortest precision that round-trips back to the same
// float32. Six digits covers nearly everything in the .ops corpus ("4",
// "0.125", "0.001", "1.7"); nine is the float32 worst case.

void wJsonWriter::Float(const sChar *key,sF32 value)
{
  Comma();
  Key(key);

  char narrow[64];
  narrow[0] = 0;
  for(int prec=6;prec<=9;prec++)
  {
    snprintf(narrow,sizeof(narrow),"%.*g",prec,double(value));
    if(strtof(narrow,0)==value)
      break;
  }

  // "-0" is a distinct float32 but an eyesore in a diff, and nothing in the
  // corpus distinguishes it from zero.
  if(narrow[0]=='-' && narrow[1]=='0' && narrow[2]==0)
  {
    narrow[0] = '0';
    narrow[1] = 0;
  }

  // %g can emit exponent form ("1e-07"), which is valid JSON, so it is passed
  // through unchanged rather than expanded.
  for(const char *s=narrow;*s;s++)
    Out->PrintChar(sChar(*s));
}

void wJsonWriter::Finish()
{
  Out->Print(L"\n");
}

/****************************************************************************/
