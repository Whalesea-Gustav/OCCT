// Created on: 1995-10-20
// Created by: Yves FRICAUD
// Copyright (c) 1995-1999 Matra Datavision
// Copyright (c) 1999-2014 OPEN CASCADE SAS
//
// This file is part of Open CASCADE Technology software library.
//
// This library is free software; you can redistribute it and/or modify it under
// the terms of the GNU Lesser General Public License version 2.1 as published
// by the Free Software Foundation, with special exception defined in the file
// OCCT_LGPL_EXCEPTION.txt. Consult the file LICENSE_LGPL_21.txt included in OCCT
// distribution for complete text of the license and disclaimer of any warranty.
//
// Alternatively, this file may be used under the terms of Open CASCADE
// commercial license or contractual agreement.


#include <Adaptor3d_Surface.hxx>
#include <BOPTools_AlgoTools.hxx>
#include <BOPTools_AlgoTools3D.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepOffset_Analyse.hxx>
#include <BRepOffset_Interval.hxx>
#include <BRepOffset_ListIteratorOfListOfInterval.hxx>
#include <BRepOffset_Tool.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <Geom2d_Curve.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <gp.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Vec.hxx>
#include <IntTools_Context.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopTools_MapOfShape.hxx>
#include <ChFi3d.hxx>

static void CorrectOrientationOfTangent(gp_Vec& TangVec,
                                        const TopoDS_Vertex& aVertex,
                                        const TopoDS_Edge& anEdge)
{
  TopoDS_Vertex Vlast = TopExp::LastVertex(anEdge);
  if (aVertex.IsSame(Vlast))
    TangVec.Reverse();
}
//=======================================================================
//function : BRepOffset_Analyse
//purpose  : 
//=======================================================================

BRepOffset_Analyse::BRepOffset_Analyse()
: myOffset (0.0), myDone (Standard_False)
{
}

//=======================================================================
//function : BRepOffset_Analyse
//purpose  : 
//=======================================================================
BRepOffset_Analyse::BRepOffset_Analyse(const TopoDS_Shape& S, 
                                       const Standard_Real Angle)
: myOffset (0.0), myDone (Standard_False)
{
  Perform( S, Angle);
}

//=======================================================================
//function : EdgeAnlyse
//purpose  : 
//=======================================================================
static void EdgeAnalyse(const TopoDS_Edge&         E,
			                  const TopoDS_Face&         F1,
			                  const TopoDS_Face&         F2,
			                  const Standard_Real        SinTol,
			                        BRepOffset_ListOfInterval& LI)
{
  
  Standard_Real   f,l;
  BRep_Tool::Range(E, F1, f, l);
  BRepOffset_Interval I;
  I.First(f); I.Last(l);
  //
  // Tangent if the regularity is at least G1.
  if (BRep_Tool::HasContinuity(E,F1,F2)) {
    if (BRep_Tool::Continuity(E,F1,F2) > GeomAbs_C0) {
      I.Type(ChFiDS_Tangential);
      LI.Append(I);
      return;
    }
  }
  //
  ChFiDS_TypeOfConcavity aType = ChFi3d::DefineConnectType(E, F1, F2,
                                                           SinTol, Standard_False);
  if(aType != ChFiDS_Tangential)
  {
    aType = ChFi3d::DefineConnectType(E, F1, F2, SinTol, Standard_True);
  }
  I.Type(aType);
  LI.Append(I);
}

//=======================================================================
//function : BuildAncestors
//purpose  : 
//=======================================================================
static void BuildAncestors (const TopoDS_Shape&                        S,
                            TopTools_IndexedDataMapOfShapeListOfShape& MA)
{  
  MA.Clear();
  TopExp::MapShapesAndUniqueAncestors(S,TopAbs_VERTEX,TopAbs_EDGE,MA);
  TopExp::MapShapesAndUniqueAncestors(S,TopAbs_EDGE  ,TopAbs_FACE,MA);
}

//=======================================================================
//function : Perform
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::Perform (const TopoDS_Shape& S, 
                                  const Standard_Real Angle)
{
  myShape = S;
  myNewFaces .Clear();
  myGenerated.Clear();
  myReplacement.Clear();
  myDescendants.Clear();

  myAngle                = Angle;
  Standard_Real SinTol = Abs (Sin(Angle));

  // Build ancestors.
  BuildAncestors (S,myAncestors);

  TopTools_ListOfShape aLETang;

  TopExp_Explorer Exp(S.Oriented(TopAbs_FORWARD),TopAbs_EDGE);
  for ( ; Exp.More(); Exp.Next()) {
    const TopoDS_Edge& E = TopoDS::Edge(Exp.Current());
    if (!myMapEdgeType.IsBound(E)) {
      BRepOffset_ListOfInterval LI;
      myMapEdgeType.Bind(E,LI);
      
      const TopTools_ListOfShape& L = Ancestors(E);
      if ( L.IsEmpty()) 
        continue;

      if (L.Extent() == 2) {
        const TopoDS_Face& F1 = TopoDS::Face (L.First());
        const TopoDS_Face& F2 = TopoDS::Face (L.Last());
        EdgeAnalyse (E, F1, F2, SinTol, myMapEdgeType (E));

        // For tangent faces add artificial perpendicular face
        // to close the gap between them (if they have different offset values)
        if (myMapEdgeType(E).Last().Type() == ChFiDS_Tangential)
          aLETang.Append (E);
      }
      else if (L.Extent() == 1) {
        Standard_Real U1, U2;
        const TopoDS_Face& F = TopoDS::Face (L.First());
        BRep_Tool::Range (E, F, U1, U2);
        BRepOffset_Interval Inter (U1, U2, ChFiDS_Other);

        if (!BRepTools::IsReallyClosed (E, F)) {
          Inter.Type (ChFiDS_FreeBound);
        }
        myMapEdgeType (E).Append (Inter);
      }
      else {  
#ifdef OCCT_DEBUG
	std::cout <<"edge shared by more than two faces"<<std::endl;
#endif	
      }
    }
  }

  TreatTangentFaces (aLETang);
  myDone = Standard_True;
}

//=======================================================================
//function : Generated
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::TreatTangentFaces (const TopTools_ListOfShape& theLE)
{
  if (theLE.IsEmpty() || myFaceOffsetMap.IsEmpty())
  {
    // Noting to do: either there are no tangent faces in the shape or
    //               the face offset map has not been provided
    return;
  }

  // Select the edges which connect faces with different offset values
  TopoDS_Compound aCETangent;
  BRep_Builder().MakeCompound (aCETangent);
  // Bind to each tangent edge a max offset value of its faces
  TopTools_DataMapOfShapeReal anEdgeOffsetMap;
  // Bind vertices of the tangent edges with connected edges
  // of the face with smaller offset value
  TopTools_DataMapOfShapeShape aDMVEMin;
  for (TopTools_ListOfShape::Iterator it (theLE); it.More(); it.Next())
  {
    const TopoDS_Shape& aE = it.Value();
    const TopTools_ListOfShape& aLA = Ancestors (aE);

    const TopoDS_Shape& aF1 = aLA.First(), aF2 = aLA.Last();

    const Standard_Real *pOffsetVal1 = myFaceOffsetMap.Seek (aF1);
    const Standard_Real *pOffsetVal2 = myFaceOffsetMap.Seek (aF2);
    const Standard_Real anOffsetVal1 = pOffsetVal1 ? Abs (*pOffsetVal1) : myOffset;
    const Standard_Real anOffsetVal2 = pOffsetVal2 ? Abs (*pOffsetVal2) : myOffset;
    if (anOffsetVal1 != anOffsetVal2)
    {
      BRep_Builder().Add (aCETangent, aE);
      anEdgeOffsetMap.Bind (aE, Max (anOffsetVal1, anOffsetVal2));

      const TopoDS_Shape& aFMin = anOffsetVal1 < anOffsetVal2 ? aF1 : aF2;
      for (TopoDS_Iterator itV (aE); itV.More(); itV.Next())
      {
        const TopoDS_Shape& aV = itV.Value();
        if (Ancestors (aV).Extent() == 3)
        {
          for (TopExp_Explorer expE (aFMin, TopAbs_EDGE); expE.More(); expE.Next())
          {
            const TopoDS_Shape& aEMin = expE.Current();
            if (aEMin.IsSame (aE))
              continue;
            for (TopoDS_Iterator itV1 (aEMin); itV1.More(); itV1.Next())
            {
              const TopoDS_Shape& aVx = itV1.Value();
              if (aV.IsSame (aVx))
                aDMVEMin.Bind (aV, aEMin);
            }
          }
        }
      }
    }
  }

  if (anEdgeOffsetMap.IsEmpty())
    return;

  // Create map of Face ancestors for the vertices on tangent edges
  TopTools_DataMapOfShapeListOfShape aDMVFAnc;

  for (TopTools_ListOfShape::Iterator itE (theLE); itE.More(); itE.Next())
  {
    const TopoDS_Shape& aE = itE.Value();
    if (!anEdgeOffsetMap.IsBound (aE))
      continue;

    TopTools_MapOfShape aMFence;
    {
      const TopTools_ListOfShape& aLEA = Ancestors (aE);
      for (TopTools_ListOfShape::Iterator itLEA (aLEA); itLEA.More(); itLEA.Next())
        aMFence.Add (itLEA.Value());
    }

    for (TopoDS_Iterator itV (aE); itV.More(); itV.Next())
    {
      const TopoDS_Shape& aV = itV.Value();
      TopTools_ListOfShape* pLFA = aDMVFAnc.Bound (aV, TopTools_ListOfShape());
      const TopTools_ListOfShape& aLVA = Ancestors (aV);
      for (TopTools_ListOfShape::Iterator itLVA (aLVA); itLVA.More(); itLVA.Next())
      {
        const TopoDS_Edge& aEA = TopoDS::Edge (itLVA.Value());
        const BRepOffset_ListOfInterval* pIntervals = myMapEdgeType.Seek (aEA);
        if (!pIntervals || pIntervals->IsEmpty())
          continue;
        if (pIntervals->First().Type() == ChFiDS_Tangential)
          continue;

        const TopTools_ListOfShape& aLEA = Ancestors (aEA);
        for (TopTools_ListOfShape::Iterator itLEA (aLEA); itLEA.More(); itLEA.Next())
        {
          const TopoDS_Shape& aFA = itLEA.Value();
          if (aMFence.Add (aFA))
            pLFA->Append (aFA);
        }
      }
    }
  }

  Handle(IntTools_Context) aCtx = new IntTools_Context();
  // Tangency criteria
  Standard_Real aSinTol = Abs (Sin (myAngle));

  // Make blocks of connected edges
  TopTools_ListOfListOfShape aLCB;
  TopTools_IndexedDataMapOfShapeListOfShape aMVEMap;

  BOPTools_AlgoTools::MakeConnexityBlocks (aCETangent, TopAbs_VERTEX, TopAbs_EDGE, aLCB, aMVEMap);

  // Analyze each block to find co-planar edges
  for (TopTools_ListOfListOfShape::Iterator itLCB (aLCB); itLCB.More(); itLCB.Next())
  {
    const TopTools_ListOfShape& aCB = itLCB.Value();

    TopTools_MapOfShape aMFence;
    for (TopTools_ListOfShape::Iterator itCB1 (aCB); itCB1.More(); itCB1.Next())
    {
      const TopoDS_Edge& aE1 = TopoDS::Edge (itCB1.Value());
      if (!aMFence.Add (aE1))
        continue;

      TopoDS_Compound aBlock;
      BRep_Builder().MakeCompound (aBlock);
      BRep_Builder().Add (aBlock, aE1.Oriented (TopAbs_FORWARD));

      Standard_Real anOffset = anEdgeOffsetMap.Find (aE1);
      const TopTools_ListOfShape& aLF1 = Ancestors (aE1);

      gp_Dir aDN1;
      BOPTools_AlgoTools3D::GetNormalToFaceOnEdge (aE1, TopoDS::Face (aLF1.First()), aDN1);

      TopTools_ListOfShape::Iterator itCB2 = itCB1;
      for (itCB2.Next(); itCB2.More(); itCB2.Next())
      {
        const TopoDS_Edge& aE2 = TopoDS::Edge (itCB2.Value());
        if (aMFence.Contains (aE2))
          continue;

        const TopTools_ListOfShape& aLF2 = Ancestors (aE2);

        gp_Dir aDN2;
        BOPTools_AlgoTools3D::GetNormalToFaceOnEdge (aE2, TopoDS::Face (aLF2.First()), aDN2);

        if (aDN1.XYZ().Crossed (aDN2.XYZ()).Modulus() < aSinTol)
        {
          BRep_Builder().Add (aBlock, aE2.Oriented (TopAbs_FORWARD));
          aMFence.Add (aE2);
          anOffset = Max (anOffset, anEdgeOffsetMap.Find (aE2));
        }
      }

      // Make the prism
      BRepPrimAPI_MakePrism aMP (aBlock, gp_Vec (aDN1.XYZ()) * anOffset);
      if (!aMP.IsDone())
        continue;

      TopTools_IndexedDataMapOfShapeListOfShape aPrismAncestors;
      TopExp::MapShapesAndAncestors (aMP.Shape(), TopAbs_EDGE, TopAbs_FACE, aPrismAncestors);
      TopExp::MapShapesAndAncestors (aMP.Shape(), TopAbs_VERTEX, TopAbs_EDGE, aPrismAncestors);

      for (TopoDS_Iterator itE (aBlock); itE.More(); itE.Next())
      {
        const TopoDS_Edge& aE = TopoDS::Edge (itE.Value());
        const TopTools_ListOfShape& aLG = aMP.Generated (aE);
        TopoDS_Face aFNew = TopoDS::Face (aLG.First());

        TopTools_ListOfShape& aLA = myAncestors.ChangeFromKey (aE);

        TopoDS_Shape aF1 = aLA.First();
        TopoDS_Shape aF2 = aLA.Last();

        const Standard_Real *pOffsetVal1 = myFaceOffsetMap.Seek (aF1);
        const Standard_Real *pOffsetVal2 = myFaceOffsetMap.Seek (aF2);
        const Standard_Real anOffsetVal1 = pOffsetVal1 ? Abs (*pOffsetVal1) : myOffset;
        const Standard_Real anOffsetVal2 = pOffsetVal2 ? Abs (*pOffsetVal2) : myOffset;

        const TopoDS_Shape& aFToRemove = anOffsetVal1 > anOffsetVal2 ? aF1 : aF2;
        const TopoDS_Shape& aFOpposite = anOffsetVal1 > anOffsetVal2 ? aF2 : aF1;

        // Orient the face so its normal is directed to smaller offset face
        {
          // get normal of the new face
          gp_Dir aDN;
          BOPTools_AlgoTools3D::GetNormalToFaceOnEdge (aE, aFNew, aDN);
        
          // get bi-normal for the aFOpposite
          TopoDS_Edge aEInF;
          for (TopExp_Explorer aExpE (aFOpposite, TopAbs_EDGE); aExpE.More(); aExpE.Next())
          {
            if (aE.IsSame (aExpE.Current()))
            {
              aEInF = TopoDS::Edge (aExpE.Current());
              break;
            }
          }
        
          gp_Pnt2d aP2d;
          gp_Pnt aPInF;
          Standard_Real f, l;
          const Handle(Geom_Curve)& aC3D = BRep_Tool::Curve (aEInF, f, l);
          gp_Pnt aPOnE = aC3D->Value ((f + l) / 2.);
          BOPTools_AlgoTools3D::PointNearEdge (aEInF, TopoDS::Face (aFOpposite), (f + l) / 2., 1.e-5, aP2d, aPInF);
        
          gp_Vec aBN (aPOnE, aPInF);
        
          if (aBN.Dot (aDN) < 0)
            aFNew.Reverse();
        }

        // Remove the face with bigger offset value from edge ancestors
        for (TopTools_ListOfShape::Iterator itA (aLA); itA.More();itA.Next())
        {
          if (itA.Value().IsSame (aFToRemove))
          {
            aLA.Remove (itA);
            break;
          }
        }
        aLA.Append (aFNew);

        myMapEdgeType (aE).Clear();
        // Analyze edge again
        EdgeAnalyse (aE, TopoDS::Face (aFOpposite), aFNew,  aSinTol, myMapEdgeType (aE));

        // Analyze vertices
        TopTools_MapOfShape aFNewEdgeMap;
        aFNewEdgeMap.Add (aE);
        for (TopoDS_Iterator itV (aE); itV.More(); itV.Next())
        {
          const TopoDS_Shape& aV = itV.Value();
          // Add Side edge to map of Ancestors with the correct orientation
          TopoDS_Edge aEG = TopoDS::Edge (aMP.Generated (aV).First());
          myGenerated.Bind (aV, aEG);
          {
            for (TopExp_Explorer anExpEg (aFNew, TopAbs_EDGE); anExpEg.More(); anExpEg.Next())
            {
              if (anExpEg.Current().IsSame (aEG))
              {
                aEG = TopoDS::Edge (anExpEg.Current());
                break;
              }
            }
          }

          if (aDMVEMin.IsBound (aV))
          {
            const TopTools_ListOfShape* pSA = aDMVFAnc.Seek (aV);
            if (pSA && pSA->Extent() == 1)
            {
              // Adjust orientation of generated edge to its new ancestor
              TopoDS_Edge aEMin = TopoDS::Edge (aDMVEMin.Find (aV));
              for (TopExp_Explorer expEx (pSA->First(), TopAbs_EDGE); expEx.More(); expEx.Next())
              {
                if (expEx.Current().IsSame (aEMin))
                {
                  aEMin = TopoDS::Edge (expEx.Current());
                  break;
                }
              }

              TopAbs_Orientation anOriInEMin (TopAbs_FORWARD), anOriInEG (TopAbs_FORWARD);
              
              for (TopoDS_Iterator itx (aEMin); itx.More(); itx.Next())
              {
                if (itx.Value().IsSame (aV))
                {
                  anOriInEMin = itx.Value().Orientation();
                  break;
                }
              }
              
              for (TopoDS_Iterator itx (aEG); itx.More(); itx.Next())
              {
                if (itx.Value().IsSame (aV))
                {
                  anOriInEG = itx.Value().Orientation();
                  break;
                }
              }
              
              if (anOriInEG == anOriInEMin)
                aEG.Reverse();
            }
          }

          TopTools_ListOfShape& aLVA = myAncestors.ChangeFromKey (aV);
          if (!aLVA.Contains (aEG))
            aLVA.Append (aEG);
          aFNewEdgeMap.Add (aEG);

          TopTools_ListOfShape& aLEGA =
            myAncestors (myAncestors.Add (aEG, aPrismAncestors.FindFromKey (aEG)));
          {
            // Add ancestors from the shape
            const TopTools_ListOfShape* pSA = aDMVFAnc.Seek (aV);
            if (pSA && !pSA->IsEmpty())
            {
              TopTools_ListOfShape aLSA = *pSA;
              aLEGA.Append (aLSA);
            }
          }

          myMapEdgeType.Bind (aEG, BRepOffset_ListOfInterval());
          if (aLEGA.Extent() == 2)
          {
            EdgeAnalyse (aEG, TopoDS::Face (aLEGA.First()), TopoDS::Face (aLEGA.Last()),
                         aSinTol, myMapEdgeType (aEG));
          }
        }

        // Find an edge opposite to tangential one and add ancestors for it
        TopoDS_Edge aEOpposite;
        for (TopExp_Explorer anExpE (aFNew, TopAbs_EDGE); anExpE.More(); anExpE.Next())
        {
          if (!aFNewEdgeMap.Contains (anExpE.Current()))
          {
            aEOpposite = TopoDS::Edge (anExpE.Current());
            break;
          }
        }

        {
          // Find it in aFOpposite
          for (TopExp_Explorer anExpE (aFToRemove, TopAbs_EDGE); anExpE.More(); anExpE.Next())
          {
            const TopoDS_Shape& aEInFToRem = anExpE.Current();
            if (aE.IsSame (aEInFToRem))
            {
              if (BOPTools_AlgoTools::IsSplitToReverse (aEOpposite, aEInFToRem, aCtx))
                aEOpposite.Reverse();
              break;
            }
          }
        }

        TopTools_ListOfShape aLFOpposite;
        aLFOpposite.Append (aFNew);
        aLFOpposite.Append (aFToRemove);
        myAncestors.Add (aEOpposite, aLFOpposite);
        myMapEdgeType.Bind (aEOpposite, BRepOffset_ListOfInterval());
        EdgeAnalyse (aEOpposite, aFNew, TopoDS::Face (aFToRemove), aSinTol, myMapEdgeType (aEOpposite));

        TopTools_DataMapOfShapeShape* pEEMap = myReplacement.ChangeSeek (aFToRemove);
        if (!pEEMap)
          pEEMap = myReplacement.Bound (aFToRemove, TopTools_DataMapOfShapeShape());
        pEEMap->Bind (aE, aEOpposite);

        // Add ancestors for the vertices
        for (TopoDS_Iterator itV (aEOpposite); itV.More(); itV.Next())
        {
          const TopoDS_Shape& aV = itV.Value();
          const TopTools_ListOfShape& aLVA = aPrismAncestors.FindFromKey (aV);
          myAncestors.Add (aV, aLVA);
        }

        myNewFaces.Append (aFNew);
        myGenerated.Bind (aE, aFNew);
      }
    }
  }
}

//=======================================================================
//function : EdgeReplacement
//purpose  : 
//=======================================================================
const TopoDS_Edge& BRepOffset_Analyse::EdgeReplacement (const TopoDS_Face& theF,
                                                        const TopoDS_Edge& theE) const
{
  const TopTools_DataMapOfShapeShape* pEE = myReplacement.Seek (theF);
  if (!pEE)
    return theE;

  const TopoDS_Shape* pE = pEE->Seek (theE);
  if (!pE)
    return theE;

  return TopoDS::Edge (*pE);
}

//=======================================================================
//function : Generated
//purpose  : 
//=======================================================================
TopoDS_Shape BRepOffset_Analyse::Generated (const TopoDS_Shape& theS) const
{
  static TopoDS_Shape aNullShape;
  const TopoDS_Shape* pGenS = myGenerated.Seek (theS);
  return pGenS ? *pGenS : aNullShape;
}

//=======================================================================
//function : Descendants
//purpose  : 
//=======================================================================
const TopTools_ListOfShape* BRepOffset_Analyse::Descendants (const TopoDS_Shape& theS,
                                                             const Standard_Boolean theUpdate) const
{
  if (myDescendants.IsEmpty() || theUpdate)
  {
    myDescendants.Clear();
    const Standard_Integer aNbA = myAncestors.Extent();
    for (Standard_Integer i = 1; i <= aNbA; ++i)
    {
      const TopoDS_Shape& aSS = myAncestors.FindKey (i);
      const TopTools_ListOfShape& aLA = myAncestors (i);

      for (TopTools_ListOfShape::Iterator it (aLA); it.More(); it.Next())
      {
        const TopoDS_Shape& aSA = it.Value();

        TopTools_ListOfShape* pLD = myDescendants.ChangeSeek (aSA);
        if (!pLD)
          pLD = myDescendants.Bound (aSA, TopTools_ListOfShape());
        if (!pLD->Contains (aSS))
          pLD->Append (aSS);
      }
    }
  }

  return myDescendants.Seek (theS);
}

//=======================================================================
//function : Clear
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::Clear()
{
  myDone = Standard_False;
  myShape     .Nullify();
  myMapEdgeType.Clear();
  myAncestors  .Clear();
  myFaceOffsetMap.Clear();
  myReplacement.Clear();
  myDescendants.Clear();
  myNewFaces .Clear();
  myGenerated.Clear();
}

//=======================================================================
//function : BRepOffset_ListOfInterval&
//purpose  : 
//=======================================================================
const BRepOffset_ListOfInterval& BRepOffset_Analyse::Type(const TopoDS_Edge& E) const 
{
  return myMapEdgeType (E);
}

//=======================================================================
//function : Edges
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::Edges(const TopoDS_Vertex&  V, 
                               const ChFiDS_TypeOfConcavity T,
                               TopTools_ListOfShape& LE) const
{
  LE.Clear();
  const TopTools_ListOfShape& L = Ancestors (V);
  TopTools_ListIteratorOfListOfShape it(L);
  
  for ( ;it.More(); it.Next()) {
    const TopoDS_Edge& E = TopoDS::Edge(it.Value());
    const BRepOffset_ListOfInterval *pIntervals = myMapEdgeType.Seek (E);
    if (pIntervals && pIntervals->Extent() > 0)
    {
      TopoDS_Vertex V1,V2;
      BRepOffset_Tool::EdgeVertices (E,V1,V2);
      if (V1.IsSame(V)) {
        if (pIntervals->Last().Type() == T)
          LE.Append (E);
      }
      if (V2.IsSame(V)) {
        if (pIntervals->First().Type() == T)
          LE.Append (E);
      }
    }
  }
}


//=======================================================================
//function : Edges
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::Edges(const TopoDS_Face&    F, 
                               const ChFiDS_TypeOfConcavity T,
                               TopTools_ListOfShape& LE) const
{
  LE.Clear();
  TopExp_Explorer exp(F, TopAbs_EDGE);

  for ( ;exp.More(); exp.Next()) {
    const TopoDS_Edge& E = TopoDS::Edge(exp.Current());

    const BRepOffset_ListOfInterval& Lint = Type(E);
    BRepOffset_ListIteratorOfListOfInterval it(Lint);
    for ( ;it.More(); it.Next()) {
      if (it.Value().Type() == T) LE.Append(E);
    }
  }
}

//=======================================================================
//function : TangentEdges
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::TangentEdges(const TopoDS_Edge&    Edge  ,
				      const TopoDS_Vertex&  Vertex,
				      TopTools_ListOfShape& Edges  ) const 
{
  gp_Vec V,VRef;

  Standard_Real U,URef;
  BRepAdaptor_Curve C3d, C3dRef;

  URef   = BRep_Tool::Parameter(Vertex,Edge);
  C3dRef = BRepAdaptor_Curve(Edge);
  VRef   = C3dRef.DN(URef,1);
  CorrectOrientationOfTangent(VRef, Vertex, Edge);
  if (VRef.SquareMagnitude() < gp::Resolution()) return;

  Edges.Clear();

  const TopTools_ListOfShape& Anc = Ancestors(Vertex);
  TopTools_ListIteratorOfListOfShape it(Anc);
  for ( ; it.More(); it.Next()) {
    const TopoDS_Edge& CurE = TopoDS::Edge(it.Value());
    if ( CurE.IsSame(Edge)) continue;
    U   = BRep_Tool::Parameter(Vertex,CurE);
    C3d = BRepAdaptor_Curve(CurE);
    V   = C3d.DN(U,1);
    CorrectOrientationOfTangent(V, Vertex, CurE);
    if (V.SquareMagnitude() < gp::Resolution()) continue;
    if (V.IsOpposite(VRef,myAngle)) {
      Edges.Append(CurE);
    }
  }
}

//=======================================================================
//function : Explode
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::Explode (TopTools_ListOfShape& List,
                                  const ChFiDS_TypeOfConcavity T) const
{
  List.Clear();
  BRep_Builder B;
  TopTools_MapOfShape Map;

  TopExp_Explorer Fexp;
  for (Fexp.Init(myShape,TopAbs_FACE); Fexp.More(); Fexp.Next()) {
    if ( Map.Add(Fexp.Current())) {
      TopoDS_Face Face = TopoDS::Face(Fexp.Current());
      TopoDS_Compound Co;
      B.MakeCompound(Co);
      B.Add(Co,Face);
      // add to Co all faces from the cloud of faces
      // G1 created from <Face>
      AddFaces(Face,Co,Map,T);
      List.Append(Co);
    }
  }
}

//=======================================================================
//function : Explode
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::Explode (TopTools_ListOfShape& List,
                                  const ChFiDS_TypeOfConcavity T1,
                                  const ChFiDS_TypeOfConcavity T2) const
{
  List.Clear();
  BRep_Builder B;
  TopTools_MapOfShape Map;
  
  TopExp_Explorer Fexp;
  for (Fexp.Init(myShape,TopAbs_FACE); Fexp.More(); Fexp.Next()) {
    if ( Map.Add(Fexp.Current())) {
      TopoDS_Face Face = TopoDS::Face(Fexp.Current());
      TopoDS_Compound Co;
      B.MakeCompound(Co);
      B.Add(Co,Face);
      // add to Co all faces from the cloud of faces
      // G1 created from  <Face>
      AddFaces(Face,Co,Map,T1,T2);
      List.Append(Co);
    }
  }
}

//=======================================================================
//function : AddFaces
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::AddFaces (const TopoDS_Face&    Face,
                                   TopoDS_Compound&      Co,
                                   TopTools_MapOfShape&  Map,
                                   const ChFiDS_TypeOfConcavity T) const
{
  BRep_Builder B;
  const TopTools_ListOfShape *pLE = Descendants (Face);
  if (!pLE)
    return;
  for (TopTools_ListOfShape::Iterator it (*pLE); it.More(); it.Next())
  {
    const TopoDS_Edge& E = TopoDS::Edge (it.Value());
    const BRepOffset_ListOfInterval& LI = Type(E);
    if (!LI.IsEmpty() && LI.First().Type() == T) {
      // so <NewFace> is attached to G1 by <Face>
      const TopTools_ListOfShape& L = Ancestors(E);
      if (L.Extent() == 2) {
        TopoDS_Face F1 = TopoDS::Face (L.First());
        if (F1.IsSame (Face))
          F1 = TopoDS::Face (L.Last());
        if (Map.Add (F1)) {
          B.Add (Co, F1);
          AddFaces (F1, Co, Map, T);
        }
      }
    }
  }
}

//=======================================================================
//function : AddFaces
//purpose  : 
//=======================================================================
void BRepOffset_Analyse::AddFaces (const TopoDS_Face&    Face,
                                   TopoDS_Compound&      Co,
                                   TopTools_MapOfShape&  Map,
                                   const ChFiDS_TypeOfConcavity T1,
                                   const ChFiDS_TypeOfConcavity T2) const
{
  BRep_Builder B;
  const TopTools_ListOfShape *pLE = Descendants (Face);
  if (!pLE)
    return;
  for (TopTools_ListOfShape::Iterator it (*pLE); it.More(); it.Next())
  {
    const TopoDS_Edge& E = TopoDS::Edge (it.Value());
    const BRepOffset_ListOfInterval& LI = Type(E);
    if (!LI.IsEmpty() && 
        (LI.First().Type() == T1 || LI.First().Type() == T2)) {
      // so <NewFace> is attached to G1 by <Face>
      const TopTools_ListOfShape& L = Ancestors(E);
      if (L.Extent() == 2) {
        TopoDS_Face F1 = TopoDS::Face (L.First());
        if (F1.IsSame (Face))
          F1 = TopoDS::Face (L.Last());
        if (Map.Add (F1)) {
          B.Add (Co, F1);
          AddFaces (F1, Co, Map, T1, T2);
        }
      }
    }
  }
}
