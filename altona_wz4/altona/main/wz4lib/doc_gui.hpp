/*+**************************************************************************/
/***                                                                      ***/
/***   Copyright (C) by Dierk Ohlerich                                    ***/
/***   all rights reserverd                                               ***/
/***                                                                      ***/
/***   To license this software, please contact the copyright holder.     ***/
/***                                                                      ***/
/**************************************************************************+*/

// The half of doc.hpp that needs the gui library and the generated shader
// library: the viewport painting context, the handle records, the parameter
// panel builder and the custom editor base class.
//
// The document model itself is in doc_core.hpp and does not need any of this.
// doc.hpp includes both halves, so existing consumers are unaffected.

#ifndef FILE_WERKKZEUG4_DOC_GUI_HPP
#define FILE_WERKKZEUG4_DOC_GUI_HPP

#ifndef __GNUC__
#pragma once
#endif

#include "wz4lib/doc_core.hpp"
#include "gui/gui.hpp"
#include "gui/listwindow.hpp"
#include "util/shaders.hpp"

/****************************************************************************/

struct wHandle                    // this structure will be rebuild every frame
{
  sInt Mode;                      // wHM_???
  sInt *t;                        // value of handle: time and position
  sF32 *x;
  sF32 *y;
  sF32 *z;
  sRect HitBox;                   // for clicking
  wOp *Op;                        // identify by Op and Id
  sInt Id;
  sInt Index;                     // index in Handles[]
  sInt SelectIndex;               // index in SelectedHandlesTag[], or -1
  sMatrix34 Local;                // transformation

  sInt ArrayLine;                 // link to gui to display array line
  sBool Selected;                 // this handle is selected

  void Clear() { sClear(*this); ArrayLine=-1; SelectIndex=-1; }
};

class wPaintInfo
{
  sGeometry *TexGeo;              // sGD_QUADLIST,sVertexFormatSingle
  sMaterial *TexMtrl;
  sMaterial *TexAMtrl;
  sTexture2D *TexDummy;

  sVertexSingle *TexGeoVP;
  sInt TexGeoVC;

  sGeometry *LineGeo2;
  sVertexBasic *LineGeoVP[2];
  sInt LineGeoVC[2];

  void PaintHandle(sInt x,sInt y,sRect &r,sBool select);
  void PaintHandlesR(wOp *parent,sBool paint);

public:
  wPaintInfo();
  ~wPaintInfo();
  // general info

  WinView *Window;                // you SHOULD not use this, but it might be usefull anyway
  wOp *Op;                        // read only (for you)
  sRect Client;                   // window client area
  sBool ClearFirst;               // does the client area need to be cleared? (for Group3D chaining)
  sInt Enable3D;                  // true:graphics.hpp / false:windows.hpp
  sTargetSpec Spec;               // sSetTarget(sTargetPara(flags,color,pi.Spec));
  sInt TimeMS;
  sInt TimeBeat;
  sBool MTMFlag;
  sTextBuffer *ViewLog;
  sInt Lod;                       // 0..3 - low / med / high / extra
  sBool CacheWarmup;              // set by player during cache warmup
  sInt CacheWarmupAgain;          // an operator indicates, that he wants the same beat warmed up again. this is used for the mandelbulb op, which needs multiple frames of warmup

  // Handles

  sBool ShowHandles;              // enable handles
  sBool Dragging;                 // dragging mode
  sBool HandleEnable;             // paint this handle. is set automatically by handle recursion
  sBool DeleteSelectedHandles;    // if this is true, please delete everything associated with a selected handle, from the handles() code
  sArray<wOp *> DeleteHandlesList;  // if DeleteSelectedHandles wants to delete an op, put it in this list for save, deferred deletion
  sInt DontPaintHandles;          // counter

  sArray<sInt> SelectedHandles;   // index into Handles[]
  sInt HandleMode;
  sU32 HandleColor;               // ARGB color used for painting.
  sInt HandleColorIndex;          // 2d color index for painting
  sRect SelectFrame;              // draw a rect as feedback for selection. just a gui service
  sBool SelectMode;

  sArray<wHandle> Handles;
  void AddHandle(wOp *op,sInt id,const sRect &r,sInt mode,sInt *t,sF32 *x,sF32 *y=0,sF32 *z=0,sInt arrayline=-1);
  void PaintHandles(const sMatrix34 *mat=0);
  void SelectHandle(wHandle *hnd,sInt mode=0);    // 0=clear all, 1=add, 2=rem
  sBool SelectionNotEmpty();
  void ClearHandleSelection();
  sBool IsSelected(wOp *op,sInt id) const;
  sInt FirstSelectedId(wOp *op) const;

  // base2d interface

  sInt PosX;                      // offset in client area
  sInt PosY;
  sInt Zoom2D;                    // 8 = normal size
  sBool Tile;
  sBool Alpha;

  // base3d interface

  sViewport *View;                // viewport. this will get manipulated by letterboxing
  sF32 Zoom3D;                    // zoom.
  sSimpleMaterial *FlatMtrl;            // sVertexFormatSingle ZON
  sSimpleMaterial *ShadedMtrl;          // sVertexFormatStandard
  sSimpleMaterial *DrawMtrl;            // sVertexFormatSingle, ZOFF
  sMaterialEnv *Env;                    // env is obsolete
  sViewport HandleView;           // viewport for painting handles
  sGeometry *LineGeo;             // sVertexFormatBasic;
  sBool Grid;
  sBool Wireframe;
  sBool CamOverride;
  sRay HitRay;
  sMatrix34 HandleTrans;          // transform handles by this
  sMatrix34 HandleTransI;          // transform handles by this
  sU32 BackColor;                 // for clearing the screen
  sU32 GridColor;
  sInt SplineMode;
  sBool SetCam;                   // overwrite interactive camera
  sMatrix34 SetCamMatrix;
  sF32 SetCamZoom;

  // misc parameters. use to pass information from "handles"/"drag" handlers to paint routine (e.g. cursor pos)

  sInt ParaI[8];
  sF32 ParaF[8];

  void ClearMisc();

  // special texture helpers

  class sImage *Image;            // use this as cache, to avoid memory allocations
  class sImage *AlphaImage;       // use this as cache, to avoid memory allocations
  sRect Rect;                     // texture rect after SetSize()


  void SetSizeTex2D(sInt xs,sInt ys); // set size of texture to calculate
  void MapTex2D(sF32 xin,sF32 yin,sInt &xout,sInt &yout);
  void PaintTex2D(sImage *img);      // do everything automatically.
  void PaintTex2D(sTexture2D *tex);
  void LineTex2D(sF32 x0,sF32 y0,sF32 x1,sF32 y1);
  void HandleTex2D(wOp *op,sInt id,sF32 &x,sF32 &y,sInt arrayline=-1);

  // special 3d helpers

  sBool Map3D(const sVector31 &pos,sF32 &x,sF32 &y,sF32 *zp);
  void Handle3D(wOp *op,sInt id,sVector31 &pos,sInt mode,sInt arrayline=-1);
  void Handle3D(wOp *op,sInt id,sF32 *x,sF32 *y,sF32 *z,sInt mode,sInt arrayline=-1);
  void Line3D(const sVector31 &a,const sVector31 &b,sU32 col=0,sBool zoff=0);
  void Transform3D(const sMatrix34 &mat);       // apply matrix to all child handles. matrix will be inverted

  // special anim helpers
/*
  sInt TimeToX(sInt time);
  sInt ValueToY(sF32 val);
  sInt XToTime(sInt x);
  sF32 YToValue(sInt y);
  void HandleAnim(wOp *op,sInt id,sInt &t,sF32 &v,sInt *sel=0,sInt arrayline=-1);
  void HandleAnim(wOp *op,sInt id,sF32 &t,sF32 &v,sInt *sel=0,sInt arrayline=-1);
*/
  // special material helpers

  void PaintMtrl();      // do everything automatically. Material must be set.
  void PaintAddRect2D(const sRect &rr,sU32 col);
  void PaintFlushRect();

  void PaintAddLine(sF32 x0,sF32 y0,sF32 z0,sF32 x1,sF32 y1,sF32 z1,sU32 col,sBool zoff);
  void PaintFlushLine(sBool zoff);
};

sOBSOLETE typedef wPaintInfo wPaintInfo3D;

/****************************************************************************/

struct wGridFrameHelper : public sGridFrameHelper
{
  wGridFrameHelper(sGridFrame *frame) : sGridFrameHelper(frame) {}
  sMessage ConnectMsg;
  sMessage LayoutMsg;
  sMessage ConnectLayoutMsg;    // both: connect and layout
  sMessage LinkBrowserMsg;          // message that opens the link browser
  sMessage LinkGotoMsg;
  sMessage LinkPopupMsg;
  sMessage LinkAnimMsg;
  sMessage AddArrayMsg;
  sMessage RemArrayMsg;
  sMessage RemArrayGroupMsg;
  sMessage FileLoadDialogMsg;
  sMessage FileSaveDialogMsg;
  sMessage ActionMsg;             //
  sMessage ArrayClearAllMsg;
  sMessage FileReloadMsg;
};

/****************************************************************************/

class wCustomEditor : public sObject
{
public:
  wCustomEditor();
  ~wCustomEditor();

  virtual void OnCalcSize(sInt &xs,sInt &ys);
  virtual void OnLayout(const sRect &Client);
  virtual void OnPaint2D(const sRect &Client);
  virtual sBool OnKey(sU32 key);
  virtual void OnDrag(const sWindowDrag &dd,const sRect &Client);
  virtual void OnChangeOp();
  virtual void OnTime(sInt time);

  void ChangeOp(wOp *op);
  void Update();
  void ScrollTo(const sRect &r,sBool safe);
};

/****************************************************************************/

#endif // FILE_WERKKZEUG4_DOC_GUI_HPP

