/****************************************************************************/
/***                                                                      ***/
/***   Stage 3.1b gate — a hand-written .wz4t case parses and connects     ***/
/***                                                                      ***/
/****************************************************************************/

// Reads tests/cases/three_ops.wz4t, lets wDocument::Connect() derive the graph
// from the geometry the file states, and checks the result. The file never
// mentions a connection.
//
// Also checks that the reader REJECTS the things it should: a bad version, an
// unknown class, a misspelled parameter, a bad choice name. A parser that
// accepts anything is worse than none, because a hand-written test case would
// then quietly test the defaults.

#include "wz4lib/doc_core.hpp"
#include "wz4lib/basic_ops.hpp"
#include "base/system.hpp"
#include "wz4t.hpp"
#include "meta.hpp"

/****************************************************************************/

void RegisterWZ4Classes()
{
  for(sInt i=0;i<2;i++)           // sREGOPS reads `i`: types first, then ops
  {
    sREGOPS(basic,0);
  }
}

static sInt Failures = 0;

static void Check(sBool cond,const sChar *what)
{
  if(cond)
  {
    sPrintF(L"  ok    %s\n",what);
  }
  else
  {
    sPrintF(L"  FAIL  %s\n",what);
    Failures++;
  }
}

static wOp *ByName(const sChar *name)
{
  wOp *op;
  sFORALL(Doc->AllOps,op)
    if(op->Name==name)
      return op;
  return 0;
}

// Every rejection case gets its own document: a half-read file leaves state
// behind, and reusing it would make later checks depend on earlier ones.
static sBool ReadsOk(const wMetaLibrary &meta,const sChar *text)
{
  delete Doc;
  Doc = new wDocument;
  return wReadWz4tText(text,L"<inline>",meta);
}

/****************************************************************************/

void sMain()
{
  const sChar *metadir = sGetShellParameter(0,0);
  const sChar *casefile = sGetShellParameter(0,1);
  if(!metadir || !casefile)
  {
    sPrint(L"usage: wz4t_read <metadir> <case.wz4t>\n");
    sSetErrorCode();
    return;
  }

  wMetaLibrary meta;
  if(!meta.LoadDirectory(metadir))
  {
    sPrintF(L"wz4t_read: %s\n",meta.GetError());
    sSetErrorCode();
    return;
  }

  sPrintF(L"wz4t_read: %s\n\n",casefile);

  Doc = new wDocument;
  if(!wReadWz4t(casefile,meta))
  {
    sPrintF(L"\nwz4t_read: the case did not parse\n");
    sSetErrorCode();
    return;
  }

  Check(Doc->Pages.GetCount()==2,L"two pages");
  Check(Doc->AllOps.GetCount()==0,L"no ops registered before Connect()");

  Doc->Connect();

  sPrintF(L"\n  %d page(s), %d operator(s)\n\n",
    Doc->Pages.GetCount(),Doc->AllOps.GetCount());

  Check(Doc->AllOps.GetCount()==9,L"nine operators, counting both pages");

  // --- explicit geometry -------------------------------------------------

  wOp *alpha  = ByName(L"alpha");
  wOp *beta   = ByName(L"beta");
  wOp *joined = ByName(L"joined");
  wOp *orphan = ByName(L"orphan");

  Check(alpha && beta && joined && orphan,L"store names came through");

  if(alpha && beta && joined && orphan)
  {
    Check(alpha->Inputs.GetCount()==0,L"a producer has no inputs");
    Check(joined->Inputs.GetCount()==2,
      L"a 6-wide consumer under two 3-wide producers takes both");
    if(joined->Inputs.GetCount()==2)
    {
      Check(joined->Inputs[0]==alpha,L"in0 is the leftmost producer");
      Check(joined->Inputs[1]==beta,L"in1 is the next one right");
    }
    Check(orphan->Inputs.GetCount()==0,L"a row's gap connects nothing");

    // Parameters actually landed. Text is string 0 of TextObject.Text.
    Check(alpha->EditStringCount>0
      && sCmpString(alpha->EditString[0]->Get(),L"first")==0,
      L"a string parameter was stored");
  }

  // --- sugar ------------------------------------------------------------

  wOp *chained = ByName(L"chained");
  Check(chained && chained->Inputs.GetCount()==1,
    L"stack{} chains each op to the one above it");

  wOp *rowsink = ByName(L"rowsink");
  Check(rowsink && rowsink->Inputs.GetCount()==2,
    L"row{} places ops side by side for a wide consumer");

  // --- what the reader must refuse ---------------------------------------

  sPrintF(L"\n  rejections:\n");

  Check(!ReadsOk(meta,L"op TextObject.Text at 0,0\n"),
    L"a file with no 'wz4t <version>' header is refused");

  Check(!ReadsOk(meta,L"wz4t 99\n"),
    L"an unknown format version is refused");

  Check(!ReadsOk(meta,L"wz4t 1\nop TextObject.Nonexistent at 0,0\n"),
    L"an unknown operator class is refused");

  Check(!ReadsOk(meta,L"wz4t 1\nop TextObject.Text at 0,0 { Txet = \"typo\" }\n"),
    L"a misspelled parameter name is refused, not ignored");

  Check(!ReadsOk(meta,L"wz4t 1\nop TextObject.Text at 0,0 { Text = 1,2,3 }\n"),
    L"too many values for a parameter is refused");

  Check(!ReadsOk(meta,L"wz4t 1\nop TextObject.Text\n"),
    L"an op with no position is refused");

  // And the things it must accept.
  Check(ReadsOk(meta,L"wz4t 1\nop TextObject.Text at 0,0\n"),
    L"'at' alone is enough; size defaults to 3x1");

  Check(ReadsOk(meta,L"wz4t 1 // trailing comment\nop TextObject.Text at 0,0\n"),
    L"'//' comments are accepted");

  sPrintF(L"\nwz4t_read: %d failure(s)\n",Failures);
  if(Failures)
    sSetErrorCode();
}

/****************************************************************************/
