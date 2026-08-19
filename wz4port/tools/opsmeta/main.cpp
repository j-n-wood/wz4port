/****************************************************************************/
/***                                                                      ***/
/***   opsmeta — .ops -> metadata JSON                                    ***/
/***                                                                      ***/
/****************************************************************************/

#include "doc.hpp"                // tools/wz4ops
#include "base/system.hpp"
#include "opsmeta.hpp"

/****************************************************************************/

void sMain()
{
  const sChar *name = sGetShellParameter(0,0);
  if(!name)
  {
    sPrintF(L"opsmeta (schema version %d)\n",OPSMETA_SCHEMA_VERSION);
    sPrint(L"usage: opsmeta name.ops\n");
    sPrint(L"reads name.ops and writes name.json\n");
    sPrint(L"\n");
    sPrint(L"Uses wz4ops' parser but not its C++ emitter. Run it from the\n");
    sPrint(L"file's own directory with a bare filename, like wz4ops.\n");
    sSetErrorCode();
    return;
  }

  Doc = new Document;
  Doc->SetNames(name);

  sString<2048> jsonname;
  sSPrintF(jsonname,L"%s.json",Doc->ProjectName);

  if(!Doc->Parse(Doc->InputFileName))
  {
    sSetErrorCode();
  }
  else
  {
    sTextBuffer json;
    if(!wEmitMetaJson(json,Doc->ProjectName))
    {
      // wEmitMetaJson has already said what it could not represent. Do not
      // write a file that a reader would trust.
      sPrintF(L"opsmeta: <%s> could not be represented; no output written\n",name);
      sSetErrorCode();
    }
    else if(!sSaveTextAnsi(jsonname,json.Get()))
    {
      sPrintF(L"opsmeta: failed to write <%s>\n",jsonname);
      sSetErrorCode();
    }
  }

  delete Doc;
}

/****************************************************************************/
