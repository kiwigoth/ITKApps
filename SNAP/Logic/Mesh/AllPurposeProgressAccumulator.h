/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    AllPurposeProgressAccumulator.h
  Language:  C++
  Date:      $Date$
  Version:   $Revision$
  Copyright (c) 2003 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.
=========================================================================*/
#ifndef __AllPurposeProgressAccumulator_h_
#define __AllPurposeProgressAccumulator_h_

#include <map>
#include <vector>
#include "itkProcessObject.h"

class vtkObject;
class vtkProcessObject;

using namespace itk;

/**
 * \class AllPurposeProgressAccumulator
 * \brief This class combines progress reports from different sources into
 * a single progress value.
 *
 * This class works as follows. A VTK or ITK process object can register with
 * this progress accumulator one or more times with different weights. When the
 * process object runs, the progress reported by it is accumulated into a total
 * progress value. The weights you supply do not need to add up to one, they
 * will be normalized automatically
 *
 * Because a source may fire more than one Start-Progress-End sequence per
 * execution, you have to advance multiple runs for one source manually using
 * the method StartNextRun.
 */
class AllPurposeProgressAccumulator : public ProcessObject
{
public:
  /** Standard class typedefs. */
  typedef AllPurposeProgressAccumulator   Self;
  typedef Object                          Superclass;
  typedef SmartPointer<Self>              Pointer;
  typedef SmartPointer<const Self>        ConstPointer;

  typedef EventObject                     EventType;

  /** Standard New method. */
  itkNewMacro(Self);

  /** Runtime information support. */
  itkTypeMacro(AllPurposeProgressAccumulator, Object);

  /** 
   * Reset the progress meter. This should be done before the entire pipeline
   * is executed for a second time. The progress will be set to zero, and the
   * first Begin event received by this class will be forwared on to the
   * observers
   */
  void ResetProgress() ;

  /**
   * Start the next run for a source that has been registered for multiple runs.
   * This should be done in a pipeline that uses the same source multiple times.
   */
  void StartNextRun(void *source);

  /** 
   * Add a VTK algorithm to the list of monitored objects
   */
  void RegisterSource(vtkProcessObject *source, float xWeight);

  /** 
   * Add an ITK algorithm to the list of monitored objects
   */
  void RegisterSource(itk::ProcessObject *source, float xWeight);

  /** Unregister a source (and all runs associated with it) */
  void UnregisterSource(vtkProcessObject *source);

  /** Unregister a source (and all runs associated with it) */
  void UnregisterSource(itk::ProcessObject *source);
  
  /** Unregister all sources and all runs */
  void UnregisterAllSources();

protected:

  AllPurposeProgressAccumulator();

private:

  // Source types
  enum SourceType { ITK, VTK };

  // Data structure associated with each component that we listen to
  struct RunData
    {
    double Weight, Progress;
    bool Started, Ended;
    };
  
  struct ProgressData
    {
    std::vector<RunData> Runs;
    unsigned int RunId;
    unsigned long StartTag, EndTag, ProgressTag;
    SourceType Type;
    };

  // Callbacks passed to ITK and VTK sources
  static void CallbackVTK(
    vtkObject *source, unsigned long eventId, void *clientdata, void *callData);
  void CallbackITK(itk::Object *comp, const EventType &event);

  // Common event-based callbacks
  void CallbackStart(void *source);
  void CallbackEnd(void *source, double progress);
  void CallbackProgress(void *source, double progress);

  // Compute total progress
  void ComputeTotalProgressAndState();

  void DebugPrint(void *, const char *, double prog = 0.0);

  // A map of weights and filter pointers
  typedef std::map<void *, ProgressData> SourceMap;
  typedef SourceMap::iterator SourceIter;
  SourceMap m_Source;

  // The overall state of the entire pipeline
  bool m_Started, m_Ended;
};

#endif
