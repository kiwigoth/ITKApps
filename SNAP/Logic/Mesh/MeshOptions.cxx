/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    MeshOptions.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$
  Copyright (c) 2003 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.
=========================================================================*/
#include "MeshOptions.h"

MeshOptions
::MeshOptions()
{
  // Begin render switches
  m_UseGaussianSmoothing = true;
  m_UseDecimation = true;
  m_UseMeshSmoothing = true;

  // Begin gsmooth params
  m_GaussianStandardDeviation = Vector3f(0.8f);
  m_GaussianError = 0.03f;

  // Begin decimate params
  m_DecimateTargetReduction = 0.95f;
  m_DecimateInitialError = 0.002f;
  m_DecimateAspectRatio = 20.0f;
  m_DecimateErrorIncrement = 0.002f;
  m_DecimateMaximumIterations = 1;
  m_DecimateFeatureAngle = 45;
  m_DecimatePreserveTopology = true;
   
  // Begin msmooth params
  m_MeshSmoothingIterations = 1;
  m_MeshSmoothingRelaxationFactor = 0.01f;
  m_MeshSmoothingFeatureAngle = 45;
  m_MeshSmoothingConvergence = 0;
  m_MeshSmoothingFeatureEdgeSmoothing = false;
  m_MeshSmoothingBoundarySmoothing = false;
}

MeshOptions
::~MeshOptions()
{
}

/*Log: MeshOptions.cxx
/*Revision 1.2  2003/07/12 01:34:18  pauly
/*More final changes before ITK checkin
/*
/*Revision 1.1  2003/07/11 23:29:17  pauly
/**** empty log message ***
/*
/*Revision 1.4  2003/06/08 16:11:42  pauly
/*User interface changes
/*Automatic mesh updating in SNAP mode
/*
/*Revision 1.3  2003/04/23 06:05:18  pauly
/**** empty log message ***
/*
/*Revision 1.2  2003/04/16 05:04:17  pauly
/*Incorporated intensity modification into the snap pipeline
/*New IRISApplication
/*Random goodies
/*
/*Revision 1.1  2003/03/07 19:29:47  pauly
/*Initial checkin
/*
/*Revision 1.1.1.1  2002/12/10 01:35:36  pauly
/*Started the project repository
/*
/*
/*Revision 1.2  2002/03/08 14:06:30  moon
/*Added Header and Log tags to all files
/**/