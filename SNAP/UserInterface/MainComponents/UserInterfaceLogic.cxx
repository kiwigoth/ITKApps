/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    UserInterfaceLogic.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$
  Copyright (c) 2003 Insight Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.
=========================================================================*/
#include "UserInterfaceLogic.h"
#include "IntensityCurveUILogic.h"
#include "GlobalState.h"
#include "GreyImageWrapper.h"
#include "IRISApplication.h"
#include "IRISImageData.h"
#include "SmoothBinaryThresholdImageFilter.h"
#include "EdgePreprocessingImageFilter.h"
#include "SNAPImageData.h"
#include "IRISVectorTypesToITKConversion.h"

// Additional UI component inludes
#include "PreprocessingUILogic.h"
#include "SnakeParametersUILogic.h"
#include "GreyImageIOWizardLogic.h"
#include "PreprocessingImageIOWizardLogic.h"
#include "SegmentationImageIOWizardLogic.h"
#include "SliceWindowCoordinator.h"
#include "SNAPLevelSetDriver.h"

#include <itksys/SystemTools.hxx>
#include <string>
#include <strstream>
#include <iomanip>

using namespace itk;
using namespace std;

UserInterfaceLogic
::UserInterfaceLogic(IRISApplication *iris, SystemInterface *system)
: UserInterface()
{
  // Store the pointers to application and system high level objects
  m_Driver = iris;
  m_SystemInterface = system;

  // This is just done for shorthand
  m_GlobalState = iris->GetGlobalState();

  // Instantiate the IO wizards
  m_WizGreyIO = new GreyImageIOWizardLogic; 
  m_WizSegmentationIO = new SegmentationImageIOWizardLogic;
  m_WizPreprocessingIO = new PreprocessingImageIOWizardLogic;
  
  // Instantiate other windows
  m_IntensityCurveUI = new IntensityCurveUILogic;

  // Initialize the preprocessing windows
  m_PreprocessingUI = new PreprocessingUILogic;
  m_PreprocessingUI->Register(this);

  // Initialize the snake parameter window
  m_SnakeParametersUI = new SnakeParametersUILogic;

  // Initialize the slice window coordinator object
  m_SliceCoordinator = new SliceWindowCoordinator();

  // Create a callback command for the snake loop
  m_PostSnakeCommand = SimpleCommandType::New();

  m_SnakeIsRunning = 0;
  init();
}

UserInterfaceLogic
::~UserInterfaceLogic() 
{
  // Delete the IO wizards
  delete m_WizGreyIO;
  delete m_WizSegmentationIO;
  delete m_WizPreprocessingIO;

  // Delete the UI's
  delete m_IntensityCurveUI;
  delete m_SnakeParametersUI;
  delete m_PreprocessingUI;

  // Delete the window coordinator
  delete m_SliceCoordinator;
}

void UserInterfaceLogic
::CloseCallback(Fl_Window* wind,void* itkNotUsed(blah))
{
  cerr << "CloseCallback" << endl;
  Fl_Window * temp = Fl::next_window(wind);

  while (temp != wind && temp != NULL) 
    {
    temp->hide();
    temp = Fl::next_window(wind);
    }

  wind->hide();
}

void 
UserInterfaceLogic
::TweakROI(Vector3i &pt1, Vector3i &pt2)
{
  Vector3i roiul,roilr;

  if (pt1[0] < pt2[0]) 
    {
    roiul[0] = pt1[0];
    roilr[0] = pt2[0];
    } 
  else 
    {
    roiul[0] = pt2[0];
    roilr[0] = pt1[0];
    }
  if (pt1[1] < pt2[1]) 
    {
    roiul[1] = pt1[1];
    roilr[1] = pt2[1];
    } 
  else 
    {
    roiul[1] = pt2[1];
    roilr[1] = pt1[1];
    }
  if (pt1[2] < pt2[2]) 
    {
    roiul[2] = pt1[2];
    roilr[2] = pt2[2];
    } 
  else 
    {
    roiul[2] = pt2[2];
    roilr[2] = pt1[2];
    }

  pt1 = roiul;
  pt2 = roilr;
}

void 
UserInterfaceLogic
::OnShowROISelect()
{
  /*
  if (ShowROI_check->value())
  {
  m_GlobalState->SetShowROI(true);
  m_IRISWindow2D[0]->EnterRegionMode();
  m_IRISWindow2D[1]->EnterRegionMode();
  m_IRISWindow2D[2]->EnterRegionMode();
  }
  else 
  {
  m_GlobalState->SetShowROI(false);
  m_IRISWindow2D[0]->ExitRegionMode();
  m_IRISWindow2D[1]->ExitRegionMode();
  m_IRISWindow2D[2]->ExitRegionMode();
  }

  RedrawWindows();

*/
}

void 
UserInterfaceLogic
::OnResetROIAction()
{
  if (!m_FileLoaded) return;

  // Get the grey image's region
  GlobalState::RegionType roi = 
    m_Driver->GetIRISImageData()->GetImageRegion();

  // The region can not be empty!
  assert(roi.GetNumberOfPixels() > 0);

  // Set the Region of interest
  m_GlobalState->SetSegmentationROI(roi);
  m_GlobalState->SetIsValidROI(true);
  
  // Update the UI
  this->RedrawWindows();
  m_OutMessage->value("Region of interest set to volume extents");
}

//--------------------------------------------
//
//
// SEGMENT 3D BUTTON CALLBACK
//
//
//--------------------------------------------

unsigned int 
UserInterfaceLogic
::GetImageAxisForDisplayWindow(unsigned int window)
{
  return m_Driver->GetCurrentImageData()->
    GetImageGeometry().GetDisplayToImageTransform(window).
    GetCoordinateIndexZeroBased(2);
}

void 
UserInterfaceLogic
::OnSnakeStartAction()
{
  uchar index = m_GlobalState->GetDrawingColorLabel();

  if (0 == index) 
    {
    fl_alert("Cannot start snake segmentation with clear color");
    return;
    }

  if (!m_Driver->GetCurrentImageData()->GetColorLabel(index).IsVisible()) 
    {
    fl_alert("Current label must be visible to start snake segmentation");
    return;
    }

  // Get the region of interest
  GlobalState::RegionType roi = m_GlobalState->GetSegmentationROI();

  // The region can not be empty
  assert(roi.GetNumberOfPixels() > 0);

  // This is where a chunk of data (ROI) gets copied
  // TODO: Clean ROI chunking up!   

  // Set the current application image mode to SNAP data
  m_Driver->InitializeSNAPImageData(roi);
  m_Driver->SetCurrentImageDataToSNAP();

  // Inform the global state that we're in sNAP
  m_GlobalState->SetSNAPActive(true);

  // Set bubble radius range according to volume dimensions (world dimensions)
  Vector3ui size = m_Driver->GetCurrentImageData()->GetVolumeExtents();
  Vector3f voxdims = m_Driver->GetSNAPImageData()->GetVoxelScaleFactor();
  float mindim = size[0]*voxdims[0];
  if (size[1]*voxdims[1] < mindim) mindim = size[1]*voxdims[1];
  if (size[2]*voxdims[2] < mindim) mindim = size[2]*voxdims[2];

  m_InBubbleRadius->value(1);
  if ((int)(mindim/2) < 1)
    m_InBubbleRadius->range(1,1);
  else 
    {
    m_InBubbleRadius->range(1,(int)(mindim/2));
    m_InBubbleRadius->value((int)(mindim/8));
    }
  m_InBubbleRadius->redraw();

  SyncSnakeToIRIS();

  //initialize GUI widgets
  m_BtnPreprocess->activate();
  m_BtnAcceptPreprocessing->deactivate();
  m_GrpSnakeChoiceRadio->activate();
  m_GrpSNAPStepInitialize->activate();
  m_GrpSnakeControl->deactivate();
  m_GrpImageOptions->deactivate();
  m_RadioSNAPViewPreprocessed->value(0);
  m_RadioSNAPViewOriginal->value(1);
  m_BtnAcceptInitialization->show();
  m_BtnRestartInitialization->hide();
  m_ChkContinuousView3DUpdate->deactivate();
  m_ChkContinuousView3DUpdate->value(0);
  m_BtnSNAPMeshUpdate->deactivate();

  // Menu items
  m_MenuLoadGrey->deactivate();
  m_MenuLoadSegmentation->deactivate();
  m_MenuSaveSegmentation->deactivate();
  m_MenuSaveGreyROI->activate();
  m_MenuLoadLabels->deactivate();
  m_MenuSaveLabels->deactivate();
  m_MenuSavePreprocessed->deactivate();
  m_MenuLoadPreprocessed->activate();
  m_MenuSaveVoxelCounts->deactivate();


  m_GlobalState->SetShowSpeed(false);

  m_BrsActiveBubbles->clear();

  uchar rgb[3];
  m_Driver->GetCurrentImageData()->GetColorLabel(index).GetRGBVector(rgb);
  m_GrpSNAPCurrentColor->color(fl_rgb_color(rgb[0], rgb[1], rgb[2]));
  m_GrpSNAPCurrentColor->redraw();

  m_SnakeStepSize = 1;
  m_InStepSize->value(0);

  m_BtnAcceptSegmentation->deactivate();

  // Set the thresholding properties to defaults
  ThresholdSettings threshSettings = 
    ThresholdSettings::MakeDefaultSettings(
      m_Driver->GetCurrentImageData()->GetGrey());

  m_GlobalState->SetThresholdSettings(threshSettings);

  // Set the thresholding properties to defaults
  EdgePreprocessingSettings edgeSettings = 
    EdgePreprocessingSettings::MakeDefaultSettings();

  m_GlobalState->SetEdgePreprocessingSettings(edgeSettings);

  // reset Mesh in IRIS window
  m_IRISWindow3D->ClearScreen();

  // show the snake window, hide the IRIS window
  ShowSNAP();

  // Image geometry has changed
  OnImageGeometryUpdate();

  m_OutMessage->value("Initalize snake");
}

//--------------------------------------------
//
//
//
//
// SNAKE WINDOW RELATED STUFF
//
//
//
//
//--------------------------------------------

//--------------------------------------------
//
//
// PREPROCESSING
//
//
//--------------------------------------------
void 
UserInterfaceLogic
::OnPreprocessAction()
{
  // Disable the 'Next' button on the preprocessing page
  m_BtnAcceptPreprocessing->deactivate();

  // Display the right window
  if(m_GlobalState->GetSnakeMode() == EDGE_SNAKE)
    {
    m_PreprocessingUI->DisplayEdgeWindow();
    }
  else
    {
    m_PreprocessingUI->DisplayInOutWindow();
    }
}

void 
UserInterfaceLogic
::OnPreprocessClose()
{
  // Check if the preprocessing image has been computed
  if(m_GlobalState->GetSpeedValid())
    {
    // Enable the 'Next' button on the preprocessing page
    m_BtnAcceptPreprocessing->activate();
    }
}

void 
UserInterfaceLogic
::OnAcceptPreprocessingAction()
{  
  SetActiveSegmentationPipelinePage(1);
}

//--------------------------------------------
//
//
// SWITCH BETWEEN GREY/PREPROC DATA
//
//
//--------------------------------------------

void 
UserInterfaceLogic
::OnSNAPViewOriginalSelect()
{
  m_GlobalState->SetShowSpeed(false);
  RedrawWindows();
}

void 
UserInterfaceLogic
::OnViewPreprocessedSelect()
{
  if (!m_GlobalState->GetSpeedValid())
    return;
  m_GlobalState->SetShowSpeed(true);
  RedrawWindows();
}

//--------------------------------------------
//
//
// BUBBLES
//
//
//--------------------------------------------

void 
UserInterfaceLogic
::OnAddBubbleAction()
{
  m_OutMessage->value("");
  Bubble* bubble;
  bubble=new Bubble();
  bubble->center=to_int(m_GlobalState->GetCrosshairsPosition());
  bubble->radius=(int) m_InBubbleRadius->value();
  char msg[1024];
  sprintf(msg,"x,y,z=%d,%d,%d; R=%d ", bubble->center[0]+1,
    bubble->center[1]+1,bubble->center[2]+1,bubble->radius);
  m_BrsActiveBubbles->add(msg, bubble);
  m_BrsActiveBubbles->value(getNumberOfBubbles()); 
  OnActiveBubblesChange();
  m_BrsActiveBubbles->redraw();
  RedrawWindows();
  cerr<<msg<<endl;
}

Bubble* 
UserInterfaceLogic
::getBubbles()
{
  int n=getNumberOfBubbles();
  Bubble* bubbles=new Bubble[n];
  for (int i=0;i<n;i++) 
    {
    bubbles[i]=*(Bubble*) m_BrsActiveBubbles->data(i+1);
    }
  return bubbles;
}

int 
UserInterfaceLogic
::getNumberOfBubbles()
{
  return m_BrsActiveBubbles->size();
}

void 
UserInterfaceLogic
::OnRemoveBubbleAction()
{
  m_OutMessage->value("");
  if (m_HighlightedBubble!=0) 
    {
    m_BrsActiveBubbles->remove(m_HighlightedBubble);
    m_HighlightedBubble=0;
    m_OutMessage->value("Bubble removed");
    RedrawWindows();
    } 
  else
    m_OutMessage->value("Highlight a bubble in browser window");

}

void 
UserInterfaceLogic
::OnActiveBubblesChange()
{
  m_OutMessage->value("");
  m_HighlightedBubble=m_BrsActiveBubbles->value();

  //  if (!m_HighlightedBubble) cerr << "problem here" << endl;

  if (m_HighlightedBubble) 
    {
    m_InBubbleRadius->value(((Bubble*) m_BrsActiveBubbles->
        data(m_HighlightedBubble))->radius);
    }
}

void 
UserInterfaceLogic
::OnBubbleRadiusChange()
{
  m_OutMessage->value("");
  if (m_HighlightedBubble!=0) 
    {
    ((Bubble*) m_BrsActiveBubbles->
     data(m_HighlightedBubble))->radius=(int) m_InBubbleRadius->value();
    char msg[1024];
    sprintf(msg,"x,y,z=%d,%d,%d; R=%d ",
      ((Bubble*) m_BrsActiveBubbles->data(m_HighlightedBubble))->
      center[0]+1, ((Bubble*) m_BrsActiveBubbles->
        data(m_HighlightedBubble))->center[1]+1,
      ((Bubble*) m_BrsActiveBubbles->data(m_HighlightedBubble))->
      center[2]+1,((Bubble*) m_BrsActiveBubbles->
        data(m_HighlightedBubble))->radius);
    m_BrsActiveBubbles->text(m_HighlightedBubble, msg);
    RedrawWindows();
    }
  else
    m_OutMessage->value("Highlight a bubble in browser window");

}

//--------------------------------------------
//
//
// SNAKE TYPE RADIO BUTTONS
//
//
//--------------------------------------------

void 
UserInterfaceLogic
::OnInOutSnakeSelect()
{
  m_RadSnakeInOut->set();
  m_RadSnakeEdge->clear();
  m_GlobalState->SetSnakeMode(IN_OUT_SNAKE);

  //Nathan Moon
  if (m_GlobalState->GetSpeedValid()) 
    {
    //make sure they want to do this
    if (0 == fl_ask("Preprocessed data will be lost!  Continue?")) 
      {
      m_RadSnakeInOut->clear();
      m_RadSnakeEdge->set();
      m_GlobalState->SetSnakeMode(EDGE_SNAKE);
      return;
      }
    }

  // Set parameters to default values
  m_GlobalState->SetSnakeParameters(
    SnakeParameters::GetDefaultInOutParameters());

  m_Driver->GetSNAPImageData()->ClearSpeed();

  m_GlobalState->SetSpeedValid(false);
  m_GrpImageOptions->deactivate();
  m_MenuSavePreprocessed->deactivate();
  m_BtnAcceptPreprocessing->deactivate();

  m_PreprocessingUI->HidePreprocessingWindows();

  m_RadioSNAPViewOriginal->setonly();
  OnSNAPViewOriginalSelect();
}

void 
UserInterfaceLogic
::OnEdgeSnakeSelect()
{
  m_RadSnakeEdge->set();
  m_RadSnakeInOut->clear();
  m_GlobalState->SetSnakeMode(EDGE_SNAKE);

  //Nathan Moon
  if (m_GlobalState->GetSpeedValid()) 
    {
    //make sure they want to do this
    if (0 == fl_ask("Preprocessed data will be lost!  Continue?")) 
      {
      m_RadSnakeInOut->set();
      m_RadSnakeEdge->clear();
      m_GlobalState->SetSnakeMode(IN_OUT_SNAKE);
      return;
      }
    }

  // Set parameters to default values
  m_GlobalState->SetSnakeParameters(
    SnakeParameters::GetDefaultEdgeParameters());

  if (m_Driver->GetSNAPImageData()) m_Driver->GetSNAPImageData()->ClearSpeed();
  m_GlobalState->SetSpeedValid(false);
  m_GrpImageOptions->deactivate();
  m_BtnAcceptPreprocessing->deactivate();
  m_MenuSavePreprocessed->deactivate();
  m_PreprocessingUI->HidePreprocessingWindows();
  m_RadioSNAPViewOriginal->setonly();

  OnSNAPViewOriginalSelect();
}

//--------------------------------------------
//
//
// SNAKE CONTROLS
//
//
//--------------------------------------------

void 
UserInterfaceLogic
::OnAcceptInitializationAction()
{
  if (!m_Driver->GetSNAPImageData()->IsSpeedLoaded()) 
    {
    m_OutMessage->value("Must preprocess image first!!");
    return;
    }

  m_BtnPreprocess->deactivate();
  m_GrpSnakeChoiceRadio->deactivate();
  m_GrpSNAPStepInitialize->deactivate();
  m_MenuLoadPreprocessed->deactivate();

  m_PreprocessingUI->HidePreprocessingWindows();

  //get bubbles, turn them into segmentation
  Bubble * bubbles = getBubbles();
  int numbubbles = getNumberOfBubbles();

  // Shorthand
  SNAPImageData *snapData = m_Driver->GetSNAPImageData();

  // Put on a wait cursor
  // TODO: Progress bar is needed here
  fl_cursor(FL_CURSOR_WAIT,FL_FOREGROUND_COLOR, FL_BACKGROUND_COLOR);

  // Merge bubbles with the segmentation image and initialize the snake
  bool rc = snapData->InitializeSegmentationPipeline(
    m_GlobalState->GetSnakeParameters(),bubbles, numbubbles,
    m_GlobalState->GetDrawingColorLabel());

  // Restore the cursor
  fl_cursor(FL_CURSOR_DEFAULT,FL_FOREGROUND_COLOR, FL_BACKGROUND_COLOR);
  
  // Check if we need to bail
  if (!rc) 
    {    
    // There were no voxels selected in the end
    fl_alert("Can not proceed without an initialization\n"
             "Please place a bubble into the image!");
    
    m_BtnPreprocess->activate();
    m_GrpSnakeChoiceRadio->activate();
    m_GrpSNAPStepInitialize->activate();

    // Clear bubble array
    if (bubbles)
      delete [] bubbles;

    return;        
    } 

  m_BtnAcceptInitialization->hide();
  m_BtnRestartInitialization->show();


  m_GrpSnakeControl->activate();
  m_ChkContinuousView3DUpdate->activate();
  m_ChkContinuousView3DUpdate->value(0);
  m_BtnSNAPMeshUpdate->activate();

  // Reset the snake iteration counter
  m_SnakeIteration = 0;
  UpdateIterationOutput();

  m_BtnAcceptSegmentation->activate();
  RedrawWindows();
  m_SNAPWindow3D->ClearScreen();// reset Mesh object in Window3D_s
  m_SNAPWindow3D->ResetView();  // reset cursor

  // Flip to the next page in the wizard
  SetActiveSegmentationPipelinePage(2);

  m_OutMessage->value("Snake initialized successfully");

  m_GlobalState->SetSnakeActive(true);

  // This is unconventional code.  At this point, the filter contained in
  // the LevelSetDriver is going to enter and execute its Update() code.  After
  // initializing, it is going to call the callback method that we construct 
  // here, and it will keep calling it, until we use the RequestEnd() method 
  // in the LevelSetDriver, which will return the control to this point in the
  // code.  That's why the snake deinitialization code is also here

  // Create the idle callback command
  SimpleCommandType::Pointer commandIdle = SimpleCommandType::New();
  commandIdle->SetCallbackFunction(
    this,&UserInterfaceLogic::OnSnakeVCRIdleCallback);

  // Create the update callback command
  SimpleCommandType::Pointer commandUpdate = SimpleCommandType::New();
  commandUpdate->SetCallbackFunction(
    this,&UserInterfaceLogic::OnSnakeVCRUpdateCallback);

  // Clear the post-snake command (command used to tell this method whom to
  // call back after it's done
  m_PostSnakeCommand = NULL;

  // TODO: Progress bar is needed here
  fl_cursor(FL_CURSOR_WAIT,FL_FOREGROUND_COLOR, FL_BACKGROUND_COLOR);
  
  // Next, enter the update loop
  m_Driver->GetSNAPImageData()->StartSegmentationPipeline(
    commandIdle,commandUpdate);

  // When we end up here, it means that the update loop has finished, which 
  // means that we've either gone back one page, or finished, or accepted the
  // segmentation.  The command pointed to by m_PostSnakeCommand will point to
  // the function that the user interface needs to execute now
  if(m_PostSnakeCommand)
    m_PostSnakeCommand->Execute((const itk::Object *) 0,itk::NoEvent());
}


void 
UserInterfaceLogic
::SetActiveSegmentationPipelinePage(unsigned int page)
{
  switch(page)
    {
    case 0 : 
      m_BtnSNAPStepPreprocess->setonly();
      m_WizSegmentationPipeline->value(m_GrpSNAPStepPreprocess);
      break;

    case 1 :
      m_BtnSNAPStepInitialize->setonly();
      m_WizSegmentationPipeline->value(m_GrpSNAPStepInitialize);
      break;

    case 2 :
      m_BtnSNAPStepSegment->setonly();
      m_WizSegmentationPipeline->value(m_GrpSNAPStepSegment);
      break;
    }
}


void 
UserInterfaceLogic
::UpdateIterationOutput()
{
  char iterstr[16];
  sprintf(iterstr,"%d",m_SnakeIteration);
  m_OutCurrentIteration->value(iterstr);
}

void 
UserInterfaceLogic
::OnRestartInitializationAction()
{
  // This callback has double functionality, depending on whether the 
  // level set update loop is active or not
  if(m_Driver->GetSNAPImageData()->IsSegmentationPipelineRunning())
    {
    // Tell the update loop to terminate
    m_Driver->GetSNAPImageData()->RequestSegmentationPipelineTermination();

    // Set the post-update callback to right here
    m_PostSnakeCommand = SimpleCommandType::New();
    m_PostSnakeCommand->SetCallbackFunction(
      this,&UserInterfaceLogic::OnRestartInitializationAction);
    }
  else
    {
    m_SnakeIsRunning = 0;
    m_GlobalState->SetSnakeActive(false);
    m_GrpSnakeControl->deactivate();
    m_BtnPreprocess->activate();
    m_GrpSnakeChoiceRadio->activate();
    m_GrpSNAPStepInitialize->activate();
    m_MenuLoadPreprocessed->activate();
    m_BtnRestartInitialization->hide();
    m_BtnAcceptInitialization->show();
    m_ChkContinuousView3DUpdate->value(0);
    m_ChkContinuousView3DUpdate->deactivate();
    m_BtnSNAPMeshUpdate->deactivate();
    m_BtnAcceptSegmentation->deactivate();

    m_SNAPWindow3D->ClearScreen(); // reset Mesh object in Window3D_s
    m_SNAPWindow3D->ResetView();   // reset cursor
    RedrawWindows();

    m_OutMessage->value("Snake initialization restarted");

    // Flip to the second page
    SetActiveSegmentationPipelinePage(1);
    }
}

void
UserInterfaceLogic
::OnRestartPreprocessingAction()
{
  // Clear the bubble list
  m_HighlightedBubble = 0;

  // Flip to the first page
  SetActiveSegmentationPipelinePage(0);

  // Repaint the screen
  RedrawWindows();
}

void 
UserInterfaceLogic
::OnSnakeParametersAction()
{
  // Get the current parameters from the system
  SnakeParameters pGlobal = m_GlobalState->GetSnakeParameters();
  
  // Send current parameter to the snake parameter setting UI
  m_SnakeParametersUI->SetParameters(pGlobal);

  // Chech whether we need to warn user about changing the solver in the 
  // process of evolution
  m_SnakeParametersUI->SetWarnOnSolverUpdate(m_SnakeIteration > 1);
  
  // Show the snake parameters window
  CenterChildWindowInParentWindow(m_SnakeParametersUI->GetWindow(),m_WinMain);
  m_SnakeParametersUI->DisplayWindow();
  
  // Wait until the window has been closed
  while(m_SnakeParametersUI->GetWindow()->visible())
    Fl::wait();
    
  // Have the parameters been accepted by the user?
  if(m_SnakeParametersUI->GetUserAccepted())
    {
    // Get the new parameters
    SnakeParameters pNew = m_SnakeParametersUI->GetParameters();
    
    // Have the parameters changed?
    if(!(pGlobal == pNew))
      {
      // Update the system's parameters with new values
      m_GlobalState->SetSnakeParameters(pNew);

      // Update the running snake
      if (m_Driver->GetSNAPImageData()->IsSegmentationPipelineInitialized()) 
        {
        m_Driver->GetSNAPImageData()->SetSegmentationParameters(pNew);
        }
      }
    }
}

void 
UserInterfaceLogic
::OnContinuousViewUpdateChange()
{
  if (m_ChkContinuousView3DUpdate->value()) 
    {
    m_BtnSNAPMeshUpdate->deactivate();
    m_SNAPWindow3D->UpdateMesh();
    }
  else 
    {
    m_BtnSNAPMeshUpdate->activate();
    }
}

void 
UserInterfaceLogic
::OnSnakeRewindAction()
{
  // Stop the play button, if it's running
  m_SnakeIsRunning = 0;
  m_SnakeIteration = 0;

  // Basically, we tell the level set driver that we want a restart
  m_Driver->GetSNAPImageData()->RequestSegmentationRestart();
}

void 
UserInterfaceLogic
::OnSnakeStopAction()
{
  // Stop the play button, if it's running
  m_SnakeIsRunning = 0;
}

void 
UserInterfaceLogic
::OnSnakeVCRUpdateCallback()
{
  // This function is called from the level set driver after an iteration has
  // been performed or after reinitialization 

  // Display the current iteration (start with 0)
  UpdateIterationOutput();

  // Update the snake mesh if continuous update is on
  if (m_ChkContinuousView3DUpdate->value())
    m_SNAPWindow3D->UpdateMesh();

  // Restore the cursor to normal
  fl_cursor(FL_CURSOR_DEFAULT,FL_FOREGROUND_COLOR, FL_BACKGROUND_COLOR);

  // Redraw the 2D windows
  RedrawWindows();

  // Increment the number of iterations
  m_SnakeIteration++;
}

void 
UserInterfaceLogic
::OnSnakeVCRIdleCallback()
{
  // If the run button is depressed, we continuously schedule runs in
  // this idle callback
  if(m_SnakeIsRunning)
    {
    // We're in run mode (run button is down).  Request more iterations
    m_Driver->GetSNAPImageData()->RequestSegmentationStep(m_SnakeStepSize);

    // Let Fltk refresh as needed
    Fl::check();
    }
  else 
    {
    // The snake is not contnuously running, so we just call Fl::wait()
    Fl::wait();    
    }  
}


void 
UserInterfaceLogic
::OnSnakePlayAction()
{
  if (!m_GlobalState->GetSnakeActive()) return;

  // Make sure the callback function is actively requesting more iterations
  m_SnakeIsRunning = 1;
}

void 
UserInterfaceLogic
::OnSnakeStepAction()
{
  // Stop the play button, if it's on
  m_SnakeIsRunning = 0;

  // Request that the desired number of iterations be executed
  m_Driver->GetSNAPImageData()->RequestSegmentationStep(m_SnakeStepSize);
}

void 
UserInterfaceLogic
::OnSnakeStepSizeChange()
{
  // Save the step size
  m_SnakeStepSize = atoi(m_InStepSize->text());
}

//--------------------------------------------
//
//
// PREPROCESSED DATA I/O
//
//
//--------------------------------------------
/*
   void UserInterfaceLogic::LoadPreprocSelectCallback()
   {
   char *m_ChosedFile = fl_file_chooser("Load Preprocessed File",NULL,NULL);
   if (m_ChosedFile)
   {
   LoadPreproc_filename->value((const char*) m_ChosedFile);
   this->SelectPreprocFileCallback();
   }
   }
   */
/*
   void UserInterfaceLogic::SelectPreprocFileCallback()
   {
// TODO: Do something about this...

if (const char* myfile = LoadPreproc_filename->value())
{
// Write header size into widget
LoadPreproc_HeaderSize->value(FileIO::GetHeaderSize((char *)myfile));

int headersize, elen, emin, emax;
Vector3f scale;
Vector3i dims, RAIcode;
FileIO::ParseHeader((char *)myfile, dims,
headersize, elen, emin, emax, dims, scale, RAIcode);

char RAIstr[3];
for (int xyz=0; xyz<3; xyz++)
{
preprocRAI[xyz] = RAIcode[xyz];//fileRAI[xyz];
RAIstr[xyz] = RAIConvert::RAIToChar(preprocRAI[xyz]);
}
PreprocRAI->value((const char *)RAIstr, 3);
}
}
*/
/*
   void UserInterfaceLogic::LoadPreprocessedDataCallback()
   {
   vtkImageData *theData;
   vtkImageData *floatData;
   Vector3i roiul, roilr;

#ifdef DEBUG
cerr << "LoadPreprocessedData" << endl;
#endif

//////// CODE FROM LOADSEGFILE_CALLBACK

LoadPreproc_win->hide();

const char *myfile = LoadPreproc_filename->value();
int headersize = (int) LoadPreproc_HeaderSize->value();

if (!this->CheckOrient(PreprocRAI->value(), preprocRAI))
{
m_OutMessage->value("Orientation syntax error");
return;
}

if (strlen(myfile) == 0)
{
m_OutMessage->value("No filename entered -- cannot load");
return;
}
else if (roi_data->ReadRawPreprocData((char *)myfile, headersize,
preprocRAI, full_data->GetDims()) !=1)
{
m_OutMessage->value("Loading preprocessed data failed");
return;
}

m_OutMessage->value("Loading preprocessed data successful");


// do the region of interest stuff
// set ROI in temp. VoxData
roiul = m_GlobalState->GetROIul();
roilr = m_GlobalState->GetROIlr();

// **** Begin Talbert Code
#ifdef DEBUG
cerr << "Before Deep Copy: " << *((float *)tmpVoxData->GetAllGreyData()->GetScalarPointer(0,0,0)) << endl;
#endif

// Used to determine if the data needs to be remapped
float newmin = 1e20;
float newmax = -1e20;

// Prepare theData for filling from the loaded data
theData = vtkImageData::New();
theData->SetScalarType(VTK_FLOAT);
theData->SetDimensions(roilr[0] - roiul[0]+1, roilr[1] - roiul[1]+1, roilr[2] - roiul[2]+1);
theData->AllocateScalars();

// pos is used as a control variable
int pos[3];

// Origdim is the size of the original preproc data set.  
// This is used for finding the min and max of the whole file
// rather than just the ROI.
int * origdim =  roi_data->GetAllPreprocData()->GetDimensions();

#ifdef DEBUG
cerr << "origdim(" << origdim[0] << "," << origdim[1] << "," << origdim[2] << ")" << endl;
#endif

// Find the min and max for remapping later on.
for(pos[2] = 0; pos[2] < origdim[2]; pos[2]++)
{
    for(pos[1] = 0; pos[1] < origdim[1]; pos[1]++)
      {
        for(pos[0] = 0; pos[0] < origdim[0]; pos[0]++)
          {
            float val = *((float *)roi_data->GetAllPreprocData()->GetScalarPointer(pos[0], pos[1], pos[2]));
            if(val < newmin)
                newmin = val;
            if(val > newmax)
                newmax = val;
        }
    }
}

// Load theData from the region of interest of the whole
// data set.
for(pos[2] = roiul[2]; pos[2] <= roilr[2]; pos[2]++)
{
    for(pos[1] = roiul[1]; pos[1] <= roilr[1]; pos[1]++)
      {
        for(pos[0] = roiul[0]; pos[0] <= roilr[0]; pos[0]++)
          {
            float * src = (float *) roi_data->GetAllPreprocData()->GetScalarPointer(pos[0], pos[1], pos[2]);
            float * dest = (float *)theData->GetScalarPointer(pos[0] - roiul[0], pos[1] - roiul[1], pos[2] - roiul[2]);
            *dest = *src;
        }
    }
}

#ifdef DEBUG
cerr << "newmin = " << newmin << endl;
cerr << *((float *)theData->GetScalarPointer(0,0,0)) << "<-UserInterfaceLogic" << endl;
cerr << theData->GetScalarType() << "," << VTK_FLOAT << endl;
cerr << "Dim: " << theData->GetDimensions()[0] << ", "
<< theData->GetDimensions()[1] << ", "
<< theData->GetDimensions()[2] << endl;
cerr << "Size: " << theData->GetActualMemorySize() << endl;
#endif

// Make data valid by setting necessary values
roi_data->SetAllPreprocData(theData);


// Make sure the data is in range for the selected
// snake
    if(m_RadSnakeInOut->value())
{
        if(-1 <= newmin && 1 >= newmax)
            roi_data->SetPreprocDataRange(-1,1);
        else
          {
            roi_data->RemapPreprocData(newmin, -1, newmax, 1);
            m_OutMessage->value("Warning: Preprocessed data was remapped to within the valid -1 to 1 range of thresholded data.");
        }
    }
    else
{
        if(0 <= newmin && 1>= newmax)
            roi_data->SetPreprocDataRange(0, 1);
        else
          {
            roi_data->RemapPreprocData(newmin, 0, newmax, 1);
            m_OutMessage->value("Warning: Preprocessed data was remapped to within the valid 0 to 1 range of edge data.");
        }
    }

// Loaded preproc data can't be saved
m_MenuSavePreprocessed->deactivate();
// **** End Talbert Code

m_GlobalState->SetSpeedValid(true);
m_GrpImageOptions->activate();
m_RadioSNAPViewPreprocessed->setonly();
OnViewPreprocessedSelect();
}

void UserInterfaceLogic::SavePreprocessedData_Callback()
{
    fl_message("Saving preprocessed data requires processing the entire volume, which may take a while.");
    SavePreproc_win->show();
}

void UserInterfaceLogic::OkaySavePreproc_button_Callback()
{
    SavePreproc_win->hide();

    const char * myfile = SavePreproc_filename->value();

#ifdef DEBUG
    cerr << "About to save preproc to " << myfile << endl;
#endif

    // Make sure we have a filename entered
    if(strlen(myfile) == 0) 
      {
        m_OutMessage->value("No filename entered.");
        return;
    }
    else 
      {
        if(m_RadSnakeInOut->value())
          {
            // Get the values needed to
            // rePreprocess the entire volume for
            // saving to file so that it is useful
            // to the user in any ROI

            // The doubling up of variables
            // is because the global state 
            // saves them as int while the preprocessing
            // uses them as float once they've been modified
            float t;
            float steepness;
            ThresholdDirection dir;
            int t2, steepness2;

            // Variable which is passed down the WritePreprocData
            // chain and returns pointing to the actual file name
            // written to (extension may differ from myfile)
            char * actualFile = NULL;

            m_GlobalState->GetPreprocInOut(t2, steepness2, dir);

            // get the threshold into the scaled range
            t = t2*vox_data->GetGreyScaleFactor() +
                vox_data->GetGreyMin();

            // get the steepness into the scaled range
            steepness = steepness2 * vox_data->GetGreyScaleFactor();

            // Call the SnakeVoxData function to write the threshold data
            if(roi_data->WritePreprocData((char *)myfile, t, steepness, dir, full_data->GetGreyImageData(), actualFile) != 1) 
              {
                cerr << "Save Failure - Something bad occurred in WritePreprocData for threshold data." << endl;
                m_OutMessage->value("Saving failed.");
                return;
            }
            else 
              {
                // Create a message for the message bar
                char temp1[] = "Saving successful to ";
                char * temp2 = new char[strlen(temp1) + strlen(actualFile) + 1];
                sprintf(temp2, "%s%s", temp1, actualFile);
                m_OutMessage->value(temp2);
                delete [] temp2;
            }
        }
        else 
          {
            // Get the values needed to
            // rePreprocess the entire volume for
            // saving to file so that it is useful
            // to the user in any ROI
            float kappa, sigma;
            int exponent;
            char * actualFile;

            m_GlobalState->GetPreprocEdge(sigma, kappa, exponent);

            //Call the SnakeVoxData function to write edge data
            if(roi_data->WritePreprocData((char *)myfile, sigma, kappa, (float) exponent, full_data->GetGreyImageData(), actualFile) != 1) 
              {
#ifdef DEBUG
                cerr << "Save Failure - Something bad occurred in WritePreprocData for edge data." << endl;
#endif
                m_OutMessage->value("Saving failed.");
                return;
            }
            else 
              {
                // Create a message for the message bar
                char temp1[] = "Saving successful to ";
                char * temp2 = new char[strlen(temp1) + strlen(actualFile) + 1];
                sprintf(temp2, "%s%s", temp1, actualFile);
                m_OutMessage->value(temp2);
                delete [] temp2;
            }
        }
    }
}
*/

//--------------------------------------------
//
//
// SWITCHING BETWEEN WINDOWS STUFF
//
//
//--------------------------------------------


void 
UserInterfaceLogic
::SyncSnakeToIRIS()
{
  m_InSNAPLabelOpacity->value(m_InIRISLabelOpacity->value());
  // Contrast_slider_s->value(Contrast_slider->value());
  // Brightness_slider_s->value(Brightness_slider->value());

  switch (m_GlobalState->GetToolbarMode()) 
    {
  case(NAVIGATION_MODE):
    m_BtnSNAPNavigation->setonly();
    break;
  default:
    SetToolbarMode(CROSSHAIRS_MODE);
    m_BtnSNAPCrosshairs->setonly();
    break;
    }
}

void 
UserInterfaceLogic
::SyncIRISToSnake()
{
  m_InIRISLabelOpacity->value(m_InSNAPLabelOpacity->value());
  // Contrast_slider->value(Contrast_slider_s->value());
  // Brightness_slider->value(Brightness_slider_s->value());

  switch (m_GlobalState->GetToolbarMode()) 
    {
  case(NAVIGATION_MODE):
    m_BtnNavigationMode->setonly();
    break;
  default:
    SetToolbarMode(CROSSHAIRS_MODE);
    m_BtnCrosshairsMode->setonly();
    break;
    }
}

void 
UserInterfaceLogic
::CloseSegmentationCommon()
{
  // This makes no sense if there is no SNAP
  assert(m_Driver->GetSNAPImageData());

  // Snake is no longer running
  m_SnakeIsRunning = 0;

  // Clean up SNAP image data
  m_Driver->SetCurrentImageDataToIRIS();
  m_Driver->ReleaseSNAPImageData();

  // Inform the global state that we're not in sNAP
  m_GlobalState->SetSNAPActive(false);

  // Speed image is no longer visible
  m_GlobalState->SetSpeedValid(false);
  m_GlobalState->SetShowSpeed(false);
  m_GlobalState->SetSnakeActive(false);

  // Updates some UI components (?)
  SyncIRISToSnake();

  // Reset the mesh display
  m_SNAPWindow3D->ClearScreen();
  m_SNAPWindow3D->ResetView();

  // Image geometry has changed
  OnImageGeometryUpdate();

  // Make sure that the SNAP 'wizard' is in the right state for 
  // returning to the application
  SetActiveSegmentationPipelinePage(0);

  // Clear the list of bubbles
  m_BrsActiveBubbles->clear();
  m_HighlightedBubble = 0;  

  // Activate/deactivate menu items
  m_MenuLoadGrey->activate();
  m_MenuLoadSegmentation->activate();
  m_MenuSaveGreyROI->deactivate();
  m_MenuSaveSegmentation->activate();
  m_MenuLoadLabels->activate();
  m_MenuSaveLabels->activate();
  m_MenuSavePreprocessed->deactivate();
  m_MenuLoadPreprocessed->deactivate();
  m_MenuSaveVoxelCounts->activate();
  
  // Show IRIS window, Hide the snake window
  ShowIRIS();

  // Redraw the windows
  RedrawWindows();
}

void 
UserInterfaceLogic
::OnAcceptSegmentationAction()
{
  // This callback has double functionality, depending on whether the 
  // level set update loop is active or not
  if(m_Driver->GetSNAPImageData()->IsSegmentationPipelineRunning())
    {
    // Tell the update loop to terminate
    m_Driver->GetSNAPImageData()->RequestSegmentationPipelineTermination();

    // Set the post-update callback to right here
    m_PostSnakeCommand = SimpleCommandType::New();
    m_PostSnakeCommand->SetCallbackFunction(
      this,&UserInterfaceLogic::OnAcceptSegmentationAction);
    }
  else
    {
    // Get data from SNAP back into IRIS
    m_Driver->UpdateIRISWithSnapImageData();

    // Close up SNAP
    this->CloseSegmentationCommon();

    // Message to the user
    m_OutMessage->value("Accepted snake segmentation");
    }  
}

void 
UserInterfaceLogic
::OnCancelSegmentationAction()
{
  // This callback has double functionality, depending on whether the 
  // level set update loop is active or not
  if(m_Driver->GetSNAPImageData()->IsSegmentationPipelineRunning())
    {
    // Tell the update loop to terminate
    m_Driver->GetSNAPImageData()->RequestSegmentationPipelineTermination();

    // Set the post-update callback to right here
    m_PostSnakeCommand = SimpleCommandType::New();
    m_PostSnakeCommand->SetCallbackFunction(
      this,&UserInterfaceLogic::OnCancelSegmentationAction);
    }
  else
    {
    // Clean up SNAP image data
    this->CloseSegmentationCommon();

    // Message to the user
    m_OutMessage->value("Snake segmentation cancelled");
    }
}

void 
UserInterfaceLogic
::shutdown()
{
  m_PreprocessingUI->HidePreprocessingWindows();
  m_WinMain->hide();
}

void 
UserInterfaceLogic
::OnMainWindowCloseAction()
{
  // We don't want to just exit when users press escape
  if(Fl::event_key() == FL_Escape) return;

  // Make sure that if the segmentation pipeline is currently running that we
  // terminate it before closing the application
  if(m_GlobalState->GetSNAPActive() &&
     m_Driver->GetSNAPImageData()->IsSegmentationPipelineRunning())
    {
    // Tell the update loop to terminate
    m_Driver->GetSNAPImageData()->RequestSegmentationPipelineTermination();

    // Set the post-update callback to right here
    m_PostSnakeCommand = SimpleCommandType::New();
    m_PostSnakeCommand->SetCallbackFunction(
      this,&UserInterfaceLogic::OnMainWindowCloseAction);
    }
  else
    {
    exit(0);
    }
}

void 
UserInterfaceLogic
::ShowIRIS()
{
  // Make sure the window is visible
  m_WinMain->show();

  // Show the right wizard page
  m_WizWindows->value(m_GrpIRISWindows);
  m_WizControlPane->value(
    m_Driver->GetCurrentImageData()->IsGreyLoaded() ? 
    m_GrpToolbarPage : m_GrpWelcomePage);

  // Show and hide the GL windows
  for(unsigned int i=0;i<3;i++)
    {
    m_IRISWindow2D[i]->show();
    m_SNAPWindow2D[i]->hide();
    }

  m_IRISWindow3D->show();
  m_SNAPWindow3D->hide();

  m_GrpCurrentColor->show();
  m_OutDrawOverColor->show();
  RedrawWindows();
}

void 
UserInterfaceLogic
::ShowSNAP()
{
  m_WizWindows->value(m_GrpSNAPWindows);
  m_WizControlPane->value(m_GrpSNAPPage);

  for(unsigned int i=0;i<3;i++)
    {
    m_SNAPWindow2D[i]->show();
    m_IRISWindow2D[i]->hide();
    }

  m_SNAPWindow3D->show();
  m_IRISWindow3D->hide();

  RedrawWindows();
}

void 
UserInterfaceLogic
::init()
{
  // Make the menu bar global
  m_MenubarMain->global();

  int i;
  // Register the GUI with its children
  for (i=0; i<3; i++) m_IRISWindow2D[i]->Register(i,this);
  m_IRISWindow3D->Register(3,this);

  for (i = 0; i < 3; i++) m_SNAPWindow2D[i]->Register(i,this);
  m_SNAPWindow3D->Register(3,this);

  // Set local variables
  m_FileLoaded = 0;
  m_SegmentationLoaded = 0;

  // Sync global state to GUI
  m_BtnCrosshairsMode->setonly();
  // SetToolbarMode(CROSSHAIRS_MODE);

  m_GlobalState->SetSegmentationAlpha(128);
  m_InIRISLabelOpacity->Fl_Valuator::value(128);

  this->InitColorMap();

  // default file and display orientation
  char RAI[3];
  int xyz;
  fileRAI = RAIConvert::GIPLToRAI(0);
  for (xyz=0; xyz<3; xyz++)
    RAI[xyz] = RAIConvert::RAIToChar(fileRAI[xyz]);
  // FileRAI->value(RAI, 3);

  strncpy(RAI, "IRP", 3);
  for (xyz=0; xyz<3; xyz++)
    intRAI[xyz] = RAIConvert::CharToRAI(RAI[xyz]);
  // IntRAI->value(RAI, 3);

  // Window title
  this->UpdateMainLabel();
  m_OutMessage->value("Welcome to SnAP; select File->Load->Grey Data to begin");

  // #include "Common/m_OutAboutCredits.h" to define char credits[]
  m_OutAboutCredits->value(credits);
  char *compil = new char[200];
  sprintf(compil, "m_OutAboutCompiled %s", __DATE__);
  m_OutAboutCompiled->value(compil);

  m_GlobalState->SetShowSpeed(false);

  m_InSNAPLabelOpacity->Fl_Valuator::value(128);

  // Initialize the highlighted bubble
  m_HighlightedBubble = 0;

  //this should probably go into the .h, a #define or something
  m_InStepSize->add("1");
  m_InStepSize->add("2");
  m_InStepSize->add("5");
  m_InStepSize->add("10");

  // Initialize the Image IO wizards
  m_WizGreyIO->MakeWindow();
  m_WizSegmentationIO->MakeWindow();
  m_WizPreprocessingIO->MakeWindow();

  // Initialize the intensity mapping curve window
  m_IntensityCurveUI->MakeWindow();

  // Create the preprocessing window
  m_PreprocessingUI->MakeWindow();

  // Create the snake parameters window
  m_SnakeParametersUI->MakeWindow();
  m_SnakeParametersUI->Register(this);
  
  // Add a listener for the events generated by the curve editor
  SimpleCommandType::TMemberFunctionPointer ptrMember = 
    & UserInterfaceLogic::OnIntensityCurveUpdate;

  SimpleCommandType::Pointer cmd = SimpleCommandType::New();
  cmd->SetCallbackFunction(this,ptrMember);
  m_IntensityCurveUI->GetEventSystem()->AddObserver(
    IntensityCurveUILogic::CurveUpdateEvent(),cmd);

  // Set the welcome page to display
  m_WizControlPane->value(m_GrpWelcomePage);

  // Callbacks
  if (m_GlobalState->GetSnakeMode() == IN_OUT_SNAKE) 
    {
    OnInOutSnakeSelect();
    }
  else 
    {
    OnEdgeSnakeSelect();
    }
}

void 
UserInterfaceLogic
::InitColorMap() 
{
  // invalidate all colormap entries
  unsigned int i,j;
  for (i=0; i<256; i++) m_ColorMap[i] = -1;
  
  // Set up the over-paint label
  m_InDrawOverColor->clear();
  m_InDrawOverColor->add("All labels");
  m_InDrawOverColor->add("Visible labels");
  m_InDrawOverColor->add("Clear label");
  
  // Set up the draw color
  m_InDrawingColor->clear();
  m_InDrawingColor->add("Clear label");
  
  m_ColorMap[0] = 0;

  j=1;
  if (m_FileLoaded) 
    {
    for (i=1; (i<256) && (j<m_Driver->GetCurrentImageData()->GetColorLabelCount()); i++) 
      {
      ColorLabel cl = m_Driver->GetCurrentImageData()->GetColorLabel(i);
      if (cl.IsValid()) 
        {
        m_ColorMap[j++] = i;
        m_InDrawOverColor->add(cl.GetLabel());
        m_InDrawingColor->add(cl.GetLabel());
        }
      }
    }

  int defcol = (j>1 ? 1 : 0);
  m_GlobalState->SetDrawingColorLabel(m_ColorMap[defcol]);
  m_GlobalState->SetOverWriteColorLabel(m_ColorMap[0]);
  m_InDrawOverColor->value(0);
  m_InDrawingColor->value(defcol);
  this->UpdateColorChips();
}

void 
UserInterfaceLogic
::RedrawWindows() 
{
  if(m_WizWindows->value() == m_GrpSNAPWindows) 
    {
    m_SNAPWindow2D[0]->redraw();
    m_SNAPWindow2D[1]->redraw();
    m_SNAPWindow2D[2]->redraw();
    m_SNAPWindow3D->redraw();
    m_GrpSNAPCurrentColor->redraw();
    }
  else
    {
    m_IRISWindow2D[0]->redraw();
    m_IRISWindow2D[1]->redraw();
    m_IRISWindow2D[2]->redraw();
    m_IRISWindow3D->redraw();
    }
}

void 
UserInterfaceLogic
::ResetScrollbars() 
{
  // Get the cursor position in image coordinated
  Vector3d cursor = to_double(m_GlobalState->GetCrosshairsPosition());

  // Update the correct scroll bars
  for (unsigned int dim=0; dim<3; dim++)
    {
    // What image axis does dim correspond to?
    unsigned int imageAxis = GetImageAxisForDisplayWindow(dim);

    if (!m_GlobalState->GetSNAPActive())
      {
      // IRIS Scrollbars (notice the negation of the value!)
      m_InIRISSliceSlider[dim]->Fl_Valuator::value( -cursor[imageAxis] );      
      }
    else
      {
      // SNAP Scrollbars (notice the negation of the value!)
      m_InSNAPSliceSlider[dim]->Fl_Valuator::value( -cursor[imageAxis] );      
      }

    // Update the little display box at the bottom of the scroll bar
    UpdatePositionDisplay(dim);
    }
}

void 
UserInterfaceLogic
::OnCrosshairPositionUpdate()
{
  this->ResetScrollbars();
  this->UpdateImageProbe();
}

void 
UserInterfaceLogic
::MakeSegTexturesCurrent() 
{
  /*
  m_IRISWindow2D[0]->MakeSegTextureCurrent();
  m_IRISWindow2D[1]->MakeSegTextureCurrent();
  m_IRISWindow2D[2]->MakeSegTextureCurrent();
  */
  m_BtnAccept3D->activate();
  this->RedrawWindows();
}

void 
UserInterfaceLogic
::UpdateColorChips()
{
  uchar index = m_GlobalState->GetDrawingColorLabel();
  uchar rgb[3];
  m_Driver->GetCurrentImageData()->GetColorLabel(index).GetRGBVector(rgb);
  m_GrpCurrentColor->color(fl_rgb_color(rgb[0], rgb[1], rgb[2]));
  m_GrpCurrentColor->redraw();

  if(m_GlobalState->GetCoverageMode() == PAINT_OVER_ONE)
    {
    index = m_GlobalState->GetOverWriteColorLabel();
    m_Driver->GetCurrentImageData()->GetColorLabel(index).GetRGBVector(rgb);
    m_OutDrawOverColor->color(fl_rgb_color(rgb[0], rgb[1], rgb[2]));
    m_OutDrawOverColor->redraw();
    }
  else
    {
    m_OutDrawOverColor->color(fl_rgb_color(192, 192, 192));
    m_OutDrawOverColor->redraw();
    }
}

void 
UserInterfaceLogic
::OnDrawingLabelUpdate()
{
  // Set the color chips
  m_GlobalState->SetDrawingColorLabel(
    (unsigned char) m_ColorMap[m_InDrawingColor->value()]);
  this->UpdateColorChips();
  this->UpdateEditLabelWindow();
}

void 
UserInterfaceLogic
::OnDrawOverLabelUpdate()
{
  

  if(m_InDrawOverColor->value() == 0)
    // The first menu item is 'paint over all'
    m_GlobalState->SetCoverageMode(PAINT_OVER_ALL);
  else if(m_InDrawOverColor->value() == 1)
    // The first menu item is 'paint over visible'
    m_GlobalState->SetCoverageMode(PAINT_OVER_COLORS);
  else
    {
    // The rest are labels
    unsigned char label = m_ColorMap[m_InDrawOverColor->value() - 2];
    m_GlobalState->SetCoverageMode(PAINT_OVER_ONE);
    m_GlobalState->SetOverWriteColorLabel(label);
    }

  this->UpdateColorChips();
}

void 
UserInterfaceLogic
::UpdateImageProbe() 
{
  // Code common to SNAP and IRIS
  Vector3ui crosshairs = m_GlobalState->GetCrosshairsPosition();
    
  // String streams for different labels
  IRISOStringStream sGrey,sSegmentation,sSpeed;

  // Get the grey intensity
  sGrey << m_Driver->GetCurrentImageData()->GetGrey()->GetVoxel(crosshairs);

  // Get the segmentation lavel intensity
  int iSegmentation = 
    (int)m_Driver->GetCurrentImageData()->GetSegmentation()->GetVoxel(crosshairs);
  sSegmentation << iSegmentation;
    

  // The rest depends on the current mode
  if(m_GlobalState->GetSNAPActive())
    {
    // Fill the grey and label outputs
    m_OutSNAPProbe->value(sGrey.str().c_str());
    m_OutSNAPLabelProbe->value(sSegmentation.str().c_str());

    // Get a pointer to the speed image wrapper
    SNAPImageData *snap = m_Driver->GetSNAPImageData();

    // Get the speed value (if possible)
    if(m_GlobalState->GetSpeedPreviewValid())
      {
      // Speed preview is being shown.  Get a preview pixel
      sSpeed << std::setprecision(4) << 
        snap->GetSpeed()->GetPreviewVoxel(crosshairs);
      }
    else if(m_GlobalState->GetSpeedValid())
      {
      // Speed image is valid, i.e., has been properly computed
      sSpeed << std::setprecision(4) << 
        snap->GetSpeed()->GetVoxel(crosshairs);
      }
    else 
      {
      sSpeed << "N/A";
      }

    // Display the speed string
    m_OutSNAPSpeedProbe->value(sSpeed.str().c_str());
    }
  else
    {
    m_OutGreyProbe->value(sGrey.str().c_str());
    m_OutLabelProbe->value(sSegmentation.str().c_str());

    // Get the label description
    ColorLabel cl = 
      m_Driver->GetCurrentImageData()->GetColorLabel(iSegmentation);
    m_OutLabelProbeText->value(cl.GetLabel());      
    }
}

void 
UserInterfaceLogic
::UpdateMainLabel() 
{
  // Will print to this label
  IRISOStringStream mainLabel;

  // Print version
  mainLabel << IRISSoftVersion << ": ";

  // Get the grey and segmentation file names
  string fnGrey = m_GlobalState->GetGreyFileName();
  string fnSeg = m_GlobalState->GetSegmentationFileName();

  // Grey file name
  if (fnGrey.length()) 
    {
    // Strip the path of the file
    mainLabel << itksys::SystemTools::GetFilenameName(fnGrey.c_str());
    }
  else
    {
    mainLabel << "no Grey img";
    }

  mainLabel << " - ";

  // Segmentation file name
  if (fnSeg.length()) 
    {
    // Strip the path of the file
    mainLabel << itksys::SystemTools::GetFilenameName(fnSeg.c_str());
    }
  else
    {
    mainLabel << "no Seg img";
    }

  // Store the label
  m_MainWindowLabel = mainLabel.str();

  // Apply to the window
  m_WinMain->label(m_MainWindowLabel.c_str());

  return;
}

/*
   void UserInterfaceLogic::SelectGreyFileCallback() 
   {
   const char *m_ChosedFile =LoadGrey_filename->value();

   int headersize, elen, emin, emax;
   Vector3i dims, defdims, RAIcode;
   Vector3f scale;
   char RAI[3];

   for (int i=0; i<3; i++) defdims[i] = 0; 

// Write values from header into the widgets
if (m_ChosedFile && FileIO::ParseHeader((char *)m_ChosedFile,
defdims,
headersize, elen, emin, emax, dims, scale, RAIcode)) 
{
HeaderSize->value(headersize);
eMin->value(emin);
eMax->value(emax);
Xdim->value(dims[0]);
Ydim->value(dims[1]);
Zdim->value(dims[2]);
Xspc->value(scale[0]);
Yspc->value(scale[1]);
Zspc->value(scale[2]);
switch (elen) 
{
case 1: eLen->value(0); break;
case 2: eLen->value(1); break;
case 4: eLen->value(2); break;
}
for (int xyz=0; xyz<3; xyz++) 
{
fileRAI[xyz] = RAIcode[xyz];
RAI[xyz] = RAIConvert::RAIToChar(RAIcode[xyz]);
}
FileRAI->value((const char *) RAI, 3);
}
}

void UserInterfaceLogic::LoadGreyFileCallback() 
{
}

void UserInterfaceLogic::SelectSegFileCallback() 
{
if (const char* myfile = LoadSeg_filename->value()) 
{
// Write header size into widget
LoadSeg_HeaderSize->value(FileIO::GetHeaderSize((char *)myfile));

int headersize, elen, emin, emax;
Vector3f scale;
Vector3i dims, RAIcode;
FileIO::ParseHeader((char *)myfile, dims, 
headersize, elen, emin, emax, dims, scale, RAIcode);

char RAIstr[3];
for (int xyz=0; xyz<3; xyz++) 
{
labelRAI[xyz] = fileRAI[xyz];
RAIstr[xyz] = RAIConvert::RAIToChar(labelRAI[xyz]);
}
SegRAI->value((const char *)RAIstr, 3);
}
}

void UserInterfaceLogic::LoadSegFileCallback() 
{
LoadSeg_win->hide();

const char *myfile = LoadSeg_filename->value();
int headersize = (int) LoadSeg_HeaderSize->value();

if (!this->CheckOrient(SegRAI->value(), labelRAI)) 
{
m_OutMessage->value("Orientation syntax error");
return;
}

if (strlen(myfile) == 0) 
{
m_OutMessage->value("No filename entered -- cannot load");
return;  
}
else if (vox_data->ReadRawSegData((char *)myfile, headersize, labelRAI) !=1) 
{
    m_OutMessage->value("Loading Seg failed");
    return;
}

m_OutMessage->value("Loading Seg successful");

m_SegmentationLoaded =1; //now have valid grey data

// Re-init other UserInterfaceLogic components
InitColorMap();
m_IRISWindow2D[0]->InitializeSlice();
m_IRISWindow2D[1]->InitializeSlice();
m_IRISWindow2D[2]->InitializeSlice();

m_IRISWindow3D->ResetView();
m_BtnMeshUpdate->activate();
RedrawWindows();
m_WinMain->redraw();

UpdateMainLabel(NULL, (char *)myfile);
}
*/
/*
void 
UserInterfaceLogic
::LoadLabelsCallback() 
{
  m_WinLoadLabel->hide();

  const char* myfile = m_InLoadLabelFilename->value();
  if (strlen(myfile) == 0) 
    {
    m_OutMessage->value("No filename entered -- cannot load");
    return;  
    }

  try 
    {
    m_Driver->GetCurrentImageData()->ReadASCIIColorLabels((char *)myfile);
    InitColorMap();
    m_BtnMeshUpdate->activate();
    MakeSegTexturesCurrent();
    m_WinMain->redraw();        
    m_OutMessage->value("Loading Label file successful");
    }
  catch (IRISException &exc) 
    {
    m_OutMessage->value(exc);
    return;
    }
}
*/

void 
UserInterfaceLogic
::UpdateEditLabelWindow() 
{
  // Get properties from VoxData
  int index = m_ColorMap[m_InDrawingColor->value()];

  // Get the color label
  ColorLabel cl = m_Driver->GetCurrentImageData()->GetColorLabel(index);
  assert(cl.IsValid());

  // Set widgets in EditLabel window
  m_InEditLabelName->value(cl.GetLabel());
  m_InEditLabelAlpha->value(cl.GetAlpha() / 255.0);
  m_InEditLabelVisibility->value(!cl.IsVisible());
  m_InEditLabelMesh->value(!cl.IsDoMesh());
  m_BtnEditLabelChange->setonly();

  // convert from uchar [0,255] to double [0.0,1.0]
  m_GrpEditLabelColorChooser->rgb( cl.GetRGB(0)/255.0, cl.GetRGB(1)/255.0, cl.GetRGB(2)/255.0 );

  // If this is clear label, assume we're going to add a label
  if (index == 0) 
    {
    m_BtnEditLabelAdd->setonly();
    m_InEditLabelName->value("New Label");
    m_InEditLabelAlpha->value(1.0);
    m_InEditLabelVisibility->clear();
    m_InEditLabelMesh->clear();
    }

  char title[100];
  sprintf(title, "Edit Color Label: Index %d", index);
  m_WinEditLabel->label(title);
}

void 
UserInterfaceLogic
::ChangeLabelsCallback() 
{
  // Check the new label name
  const char* new_name = m_InEditLabelName->value();
  if (strlen(new_name) == 0) 
    {
    m_OutMessage->value("You must enter a non-null name");
    return;
    }

  // Update the label Choice widgets
  int offset;
  if (m_BtnEditLabelAdd->value() == 1) 
    {  // add a new label
    offset = m_InDrawingColor->add(new_name);
    m_InDrawOverColor->add(new_name);   
    }
  else 
    {  // Replace an old label
    offset = m_InDrawingColor->value();
    if (!offset) 
      {
      m_OutMessage->value("You cannot recolor the clear color");
      return;
      }
    m_InDrawingColor->replace(offset, new_name);
    m_InDrawOverColor->replace(offset+2, new_name);
    }
  m_InDrawingColor->value(offset);
  m_InDrawingColor->set_changed();

  // Search for a place to put this new label
  if (m_ColorMap[offset] <0) 
    {
    int i;
    for (i=1; i<256 && m_Driver->GetCurrentImageData()->GetColorLabel(i).IsValid(); i++) 
      {
      }

    m_ColorMap[offset] = i;
    }

  unsigned char rgb[3];
  rgb[0] = (uchar) ( 255*m_GrpEditLabelColorChooser->r() );
  rgb[1] = (uchar) ( 255*m_GrpEditLabelColorChooser->g() );
  rgb[2] = (uchar) ( 255*m_GrpEditLabelColorChooser->b() );

  // Send changes to the Voxel Data Structure
  ColorLabel cl = m_Driver->GetCurrentImageData()->GetColorLabel(m_ColorMap[offset]);
  cl.SetRGBVector(rgb);
  cl.SetAlpha(static_cast<unsigned char>(255 * m_InEditLabelAlpha->value()));
  cl.SetVisible(!m_InEditLabelVisibility->value());
  cl.SetDoMesh(!m_InEditLabelMesh->value());
  cl.SetLabel(new_name);
  cl.SetValid(true);

  // Make sure that the display windows get the updated color labels
  m_Driver->GetCurrentImageData()->SetColorLabel(m_ColorMap[offset],cl);

  // Set the new current color in the GUI
  m_GrpCurrentColor->color(fl_rgb_color(rgb[0], rgb[1], rgb[2]));

  MakeSegTexturesCurrent();
  m_GlobalState->SetDrawingColorLabel((unsigned char) m_ColorMap[offset]);

  // Redraw the windows 
  this->RedrawWindows();
  m_WinMain->redraw();
}

void 
UserInterfaceLogic
::OnSliceSliderChange(int id) 
{
  // Get the new value depending on the current state
  unsigned int value = (unsigned int)
    (m_GlobalState->GetSNAPActive() ? 
     - m_InSNAPSliceSlider[id]->value() :
     - m_InIRISSliceSlider[id]->value());
  
  // Get the cursor position
  Vector3ui cursor = m_GlobalState->GetCrosshairsPosition();

  // Determine which image axis the display window 'id' corresponds to
  unsigned int imageAxis = GetImageAxisForDisplayWindow(id);

  // Update the cursor
  cursor[imageAxis] = value;

  // TODO: Unify this!
  m_Driver->GetCurrentImageData()->SetCrosshairs(cursor);
  m_GlobalState->SetCrosshairsPosition(cursor);

  // Update the little display box under the scroll bar
  UpdatePositionDisplay(id);

  // Repaint the windows
  RedrawWindows();
}

void 
UserInterfaceLogic
::UpdatePositionDisplay(int id) 
{
  // Dump out the slice index of the given window
  IRISOStringStream sIndex;

  // Code depends on SNAP/IRIS mode
  if(m_GlobalState->GetSNAPActive())
    {
    sIndex << std::setw(4) << (1 - m_InSNAPSliceSlider[id]->value());
    sIndex << " of ";
    sIndex << (1 - m_InSNAPSliceSlider[id]->minimum());
    m_OutSNAPSliceIndex[id]->value(sIndex.str().c_str());
    }
  else
    {
    sIndex << std::setw(4) << (1 - m_InIRISSliceSlider[id]->value());
    sIndex << " of ";
    sIndex << (1 - m_InIRISSliceSlider[id]->minimum());
    m_OutIRISSliceIndex[id]->value(sIndex.str().c_str());
    }    
}

void 
UserInterfaceLogic
::OnAcceptPolygonAction(unsigned int window)
{
  m_IRISWindow2D[window]->AcceptPolygon();
  OnPolygonStateUpdate(window);
  
  MakeSegTexturesCurrent();
  m_BtnMeshUpdate->activate();
}

void 
UserInterfaceLogic
::OnInsertIntoPolygonSelectedAction(unsigned int window)
{
  m_IRISWindow2D[window]->InsertPolygonPoints();  
  OnPolygonStateUpdate(window);
}

void 
UserInterfaceLogic
::OnDeletePolygonSelectedAction(unsigned int window)
{
  m_IRISWindow2D[window]->DeleteSelectedPolygonPoints();
  OnPolygonStateUpdate(window);
}

void 
UserInterfaceLogic
::OnPastePolygonAction(unsigned int window)
{
  m_IRISWindow2D[window]->PastePolygon();
  OnPolygonStateUpdate(window);
}

void 
UserInterfaceLogic
::OnPolygonStateUpdate(unsigned int id)
{
  // Get the drawing object
  PolygonDrawing *draw = m_IRISWindow2D[id]->GetPolygonDrawing();

  if (draw->GetState() == INACTIVE_STATE) 
    {
    // There is no active polygon
    m_BtnPolygonAccept[id]->deactivate();
    m_BtnPolygonDelete[id]->deactivate();
    m_BtnPolygonInsert[id]->deactivate();

    // Check if there is a cached polygon for pasting
    if(draw->CachedPolygon()) 
      m_BtnPolygonPaste[id]->activate();
    else
      m_BtnPolygonPaste[id]->deactivate();      
    }
  else if(draw->GetState() == DRAWING_STATE)
    {
    // There is no active polygon
    m_BtnPolygonAccept[id]->deactivate();
    m_BtnPolygonDelete[id]->deactivate();
    m_BtnPolygonInsert[id]->deactivate();
    m_BtnPolygonPaste[id]->deactivate();
    }
  else
    {
    m_BtnPolygonAccept[id]->activate();
    m_BtnPolygonPaste[id]->deactivate();

    if(draw->GetSelectedVertices() > 0)
      {
      m_BtnPolygonDelete[id]->activate();
      m_BtnPolygonInsert[id]->activate();
      }
    else
      {
      m_BtnPolygonDelete[id]->deactivate();
      m_BtnPolygonInsert[id]->deactivate();
      }
    }
}
/*
void 
UserInterfaceLogic
::ActivatePaste(bool on) 
{
  for (int xyz=0; xyz<3; xyz++)
    this->ActivatePaste(xyz, on);
  cerr << "activating pastes..." << endl;
}

void 
UserInterfaceLogic
::ActivateAccept(bool on) 
{
  for (int xyz=0; xyz<3; xyz++)
    this->ActivateAccept(xyz, on);
}
*/



void 
UserInterfaceLogic
::ApplyRenderingOptions() 
{
  // Get the current mesh options
  MeshOptions mops;

  // Set the Gaussian properties
  mops.SetGaussianStandardDeviation(
    m_InRenderOptionsGaussianStandardDeviation->value());
  mops.SetUseGaussianSmoothing(
    m_InUseGaussianSmoothing->value());
  mops.SetGaussianError(
    m_InRenderOptionsGaussianError->value());

  // Triangle Decimation
  mops.SetUseDecimation(
    m_InUseDecimate->value());
  mops.SetDecimateAspectRatio(
    m_InRenderOptionsDecimateAspectRatio->value());
  mops.SetDecimateErrorIncrement(
    m_InRenderOptionsDecimateErrorIncrement->value());
  mops.SetDecimateFeatureAngle(
    m_InRenderOptionsDecimateFeatureAngle->value());
  mops.SetDecimateInitialError(
    m_InRenderOptionsDecimateInitialError->value());
  mops.SetDecimateMaximumIterations(
    (unsigned int)m_InRenderOptionsDecimateIterations->value());
  mops.SetDecimatePreserveTopology(
    m_InRenderOptionsDecimateTopology->value());
  mops.SetDecimateTargetReduction(
    m_InRenderOptionsDecimateReductions->value());

  // Mesh Smoothing
  mops.SetUseMeshSmoothing(
    m_InUseMeshSmoothing->value());  
  mops.SetMeshSmoothingBoundarySmoothing(
    m_InRenderOptionsMeshSmoothBoundarySmoothing->value());
  mops.SetMeshSmoothingConvergence(
    m_InRenderOptionsMeshSmoothConvergence->value());
  mops.SetMeshSmoothingFeatureAngle(
    m_InRenderOptionsMeshSmoothFeatureAngle->value());
  mops.SetMeshSmoothingFeatureEdgeSmoothing(
    m_InRenderOptionsMeshSmoothFeatureEdge->value());
  mops.SetMeshSmoothingIterations(
    (unsigned int)m_InRenderOptionsMeshSmoothIterations->value());
  mops.SetMeshSmoothingRelaxationFactor(
    m_InRenderOptionsMeshSmoothRelaxation->value());

  // Save the mesh options
  m_GlobalState->SetMeshOptions(mops);
}

void 
UserInterfaceLogic
::FillRenderingOptions() 
{
  // Get the current mesh options
  MeshOptions mops = m_GlobalState->GetMeshOptions();

  // Set the Gaussian properties
  m_InRenderOptionsGaussianStandardDeviation->value(
    mops.GetGaussianStandardDeviation());
  m_InUseGaussianSmoothing->value(
    mops.GetUseGaussianSmoothing());
  m_InRenderOptionsGaussianError->value(
    mops.GetGaussianError());

  // Triangle Decimation
  m_InUseDecimate->value(
    mops.GetUseDecimation());
  m_InRenderOptionsDecimateAspectRatio->value(
    mops.GetDecimateAspectRatio());
  m_InRenderOptionsDecimateErrorIncrement->value(
    mops.GetDecimateErrorIncrement());
  m_InRenderOptionsDecimateFeatureAngle->value(
    mops.GetDecimateFeatureAngle());
  m_InRenderOptionsDecimateInitialError->value(
    mops.GetDecimateInitialError());
  m_InRenderOptionsDecimateIterations->value(
    (double)mops.GetDecimateMaximumIterations());
  m_InRenderOptionsDecimateTopology->value(
    mops.GetDecimatePreserveTopology());
  m_InRenderOptionsDecimateReductions->value(
    mops.GetDecimateTargetReduction());

  // Mesh Smoothing
  m_InUseMeshSmoothing->value(
    mops.GetUseMeshSmoothing());  
  m_InRenderOptionsMeshSmoothBoundarySmoothing->value(
    mops.GetMeshSmoothingBoundarySmoothing());
  m_InRenderOptionsMeshSmoothConvergence->value(
    mops.GetMeshSmoothingConvergence());
  m_InRenderOptionsMeshSmoothFeatureAngle->value(
    mops.GetMeshSmoothingFeatureAngle());
  m_InRenderOptionsMeshSmoothFeatureEdge->value(
    mops.GetMeshSmoothingFeatureEdgeSmoothing());
  m_InRenderOptionsMeshSmoothIterations->value(
    (double)mops.GetMeshSmoothingIterations());
  m_InRenderOptionsMeshSmoothRelaxation->value(
    mops.GetMeshSmoothingRelaxationFactor());
}

void 
UserInterfaceLogic
::FillSliceLayoutOptions() 
{
  // Hack
  if(!m_OutDisplayOptionsPanel[0]->value() ||
     strlen(m_OutDisplayOptionsPanel[0]->value()) == 0)
    {
    OnSliceAnatomyOptionsChange(0);
    }
}


void 
UserInterfaceLogic
::OnMenuShowDisplayOptions()
{
  FillSliceLayoutOptions();
  FillRenderingOptions();
  m_WinDisplayOptions->show();
}

void 
UserInterfaceLogic
::OnDisplayOptionsCancelAction()
{
  this->m_WinDisplayOptions->hide();
}

void 
UserInterfaceLogic
::OnDisplayOptionsOkAction()
{
  OnDisplayOptionsApplyAction();
  this->m_WinDisplayOptions->hide();
}

void 
UserInterfaceLogic
::OnDisplayOptionsApplyAction()
{
  ApplySliceLayoutOptions();
  ApplyRenderingOptions();
}


void 
UserInterfaceLogic
::ApplySliceLayoutOptions()
{
  unsigned int order[6][3] = 
    {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};

  // Search for the selected orientation toggle
  unsigned int i;
  for(i=0;i<6;i++)
    if(m_BtnOptionsViews2D[i]->value())
      break;

  // Make sure something is selected
  if(i == 6) return;

  // Start with stock orientations
  string axes[3] = {string("RPS"),string("AIL"),string("RIP")};

  // Switch the configurable directions
  if(m_ChkOptionsViews2DRightIsLeft->value() == 0)
    {
    axes[0][0] = axes[2][0] = 'L';
    }
  if(m_ChkOptionsViews2DNoseLeft->value() == 0)
    {
    axes[1][0] = 'P';
    }

  // Check if the configuration is different
  if(axes[order[i][0]] != string(m_Driver->GetDisplayToAnatomyRAI(0)) ||
     axes[order[i][1]] != string(m_Driver->GetDisplayToAnatomyRAI(1)) ||
     axes[order[i][2]] != string(m_Driver->GetDisplayToAnatomyRAI(2)))
    {
    // Assign the configuration
    m_Driver->SetDisplayToAnatomyRAI(axes[order[i][0]].c_str(),
                                     axes[order[i][1]].c_str(),
                                     axes[order[i][2]].c_str());
    // Reassign slices to windows
    OnImageGeometryUpdate();

    // Redraw the windows
    RedrawWindows();
    }
}

void 
UserInterfaceLogic
::OnSliceAnatomyOptionsChange(unsigned int option)
{
  static const char *labels[3] = {"Axial","Sagittal","Coronal"};
  unsigned int order[6][3] = 
    {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};

  for(unsigned int i=0;i<3;i++)
    m_OutDisplayOptionsPanel[i]->value(labels[order[option][i]]);
}

void
UserInterfaceLogic
::OnImageGeometryUpdate()
{
  if(!m_Driver->GetCurrentImageData()->IsGreyLoaded())
    return;

  // Set the crosshairs to the center of the image
  Vector3ui size = m_Driver->GetCurrentImageData()->GetVolumeExtents();
  Vector3ui xCross = size / ((unsigned int) 2);
  m_GlobalState->SetCrosshairsPosition(xCross);
  m_Driver->GetCurrentImageData()->SetCrosshairs(xCross);

  // Update the crosshairs display
  this->OnCrosshairPositionUpdate();

  // Update the source for slice windows as well as scroll bars
  if(m_GlobalState->GetSNAPActive())
    {
    for (unsigned int i=0; i<3; i++) 
      {
      // Connect slices to windows
      m_SNAPWindow2D[i]->InitializeSlice(m_Driver->GetCurrentImageData());
      
      // Get the image axis that corresponds to the display window i
      unsigned int imageAxis = GetImageAxisForDisplayWindow(i);

      // Notice the sliders have a negative range!  That's so that the 1 position is at the 
      // bottom.  We need to always negate the slider values
      m_InSNAPSliceSlider[i]->range( 1.0 - size[imageAxis], 0.0 );
      m_InSNAPSliceSlider[i]->slider_size( 1.0/ size[imageAxis] );
      m_InSNAPSliceSlider[i]->linesize(1);
      m_OutSNAPSliceIndex[i]->activate();
      }
    }
  else
    {
    for (unsigned int i=0; i<3; i++) 
      {
      m_IRISWindow2D[i]->InitializeSlice(m_Driver->GetCurrentImageData());
      
      // Get the image axis that corresponds to the display window i
      unsigned int imageAxis = GetImageAxisForDisplayWindow(i);

      // As with the other sliders, the range is negative
      m_InIRISSliceSlider[i]->range( 1.0 - size[imageAxis], 0.0 );
      m_InIRISSliceSlider[i]->slider_size( 1.0/size[imageAxis] );
      m_InIRISSliceSlider[i]->linesize(1);
      m_OutIRISSliceIndex[i]->activate();
      }      
    }

  // Group the three windows inside the window coordinator
  m_SliceCoordinator->RegisterWindows(
    reinterpret_cast<GenericSliceWindow **>(m_IRISWindow2D));    

  // Reset the view in 2D windows to fit
  m_SliceCoordinator->ResetViewToFitInAllWindows();
}
  
int 
UserInterfaceLogic
::CheckOrient(const char *txt, Vector3i &RAI) 
{
  if (strlen(txt) != 3) 
    {
    fl_alert("Expecting a 3-character code, e.g. RAI");
    return 0;
    }

  int i;
  Vector3i tmpRAI;
  for (i=0; i<3; i++) 
    {
    tmpRAI[i] = RAIConvert::CharToRAI(txt[i]);
    if (!tmpRAI[i]) 
      {
      fl_alert("Only characters R/L, A/P, I/S allowed"); return 0;
      }
    }
  int check = tmpRAI[0]*tmpRAI[1]*tmpRAI[2];
  if (check != 6 && check != -6) 
    {
    fl_alert("Must specify all axes: R/L, A/P, I/S"); return 0;
    }

  for (i=0; i<3; i++) RAI[i] = tmpRAI[i];
  return 1;
}
void 
UserInterfaceLogic
::Activate3DAccept(bool on) 
{
  if (on) m_BtnAccept3D->activate();
  else m_BtnAccept3D->deactivate();
}

void 
UserInterfaceLogic
::SaveLabelsCallback() 
{
  m_WinSaveLabel->hide();

  const char* myfile = m_InSaveLabelFilename->value();
  if (strlen(myfile) == 0) 
    {
    m_OutMessage->value("No filename entered - cannot save");
    return;
    }

  try 
    {
    m_Driver->WriteLabelDescriptionsToTextFile(myfile);
    m_OutMessage->value("Saving Labels Successful");
    }
  catch (IRISException &exc) 
    {
    m_OutMessage->value(exc);
    }
}

void 
UserInterfaceLogic
::SetToolbarMode(ToolbarModeType mode)
{
  // There must be an active image before we do this stuff
  assert(m_Driver->GetCurrentImageData()->IsGreyLoaded());

  // Set the mode on the toolbar
  m_GlobalState->SetToolbarMode(mode);

  // Set the mode of each window
  for(unsigned int i=0;i<3;i++)
    {
    switch(mode) 
      {
    case CROSSHAIRS_MODE:
      m_IRISWindow2D[i]->EnterCrosshairsMode();
      m_SNAPWindow2D[i]->EnterCrosshairsMode();
      m_TabsToolOptions->value(m_GrpToolOptionCrosshairs);
      break;

    case NAVIGATION_MODE:
      m_IRISWindow2D[i]->EnterZoomPanMode();
      m_SNAPWindow2D[i]->EnterZoomPanMode();
      m_TabsToolOptions->value(m_GrpToolOptionZoomPan);
      break;

    case POLYGON_DRAWING_MODE:
      m_IRISWindow2D[i]->EnterPolygonMode();
      m_TabsToolOptions->value(m_GrpToolOptionPolygon);
      break;

    case PAINT3D_MODE:
      m_IRISWindow2D[i]->EnterCrosshairsMode();
      m_TabsToolOptions->value(m_GrpToolOptionPaintCan);
      break;

    case ROI_MODE:
      m_IRISWindow2D[i]->EnterRegionMode();
      m_TabsToolOptions->value(m_GrpToolOptionSnAP);
      break;

    default:
      break;  
      }

    // Enable the polygon editing windows
    if(mode == POLYGON_DRAWING_MODE)
      m_GrpPolygonEdit[i]->show();
    else
      m_GrpPolygonEdit[i]->hide();

    }

  // Redraw the windows
  RedrawWindows();
}

void 
UserInterfaceLogic
::PrintVoxelCountsCallback() 
{
  const char *chosenFile = 
    fl_file_chooser("Voxel Count Output",
                    "Text Files (*.txt)","voxels.txt");
  if(chosenFile)
  {
    try 
    {
      m_Driver->GetCurrentImageData()->CountVoxels(chosenFile);
      m_OutMessage->value("Voxel Count successful");
    }
    catch(itk::ExceptionObject &exc)
    {
      fl_alert("Error writing voxel counts: %s",exc.GetDescription());
    }
  }
}

void 
UserInterfaceLogic
::OnGreyImageUpdate()
{
  // Blank the screen - useful on a load of new grey data when there is 
  // already a segmentation file present
  m_IRISWindow3D->ClearScreen(); 

  // Flip over to the toolbar page
  m_WizControlPane->value(m_GrpToolbarPage);

  // Enable some menu items
  m_MenuLoadSegmentation->activate();
  m_MenuSaveSegmentation->activate();
  m_MenuSaveVoxelCounts->activate(); 
  m_MenuIntensityCurve->activate();

  // Image geometry has changed
  OnImageGeometryUpdate();

  // We now have valid grey data
  m_FileLoaded = 1;
  m_SegmentationLoaded = 0;

  InitColorMap();
  m_InDrawingColor->set_changed();
  m_InDrawOverColor->set_changed();

  m_GlobalState->SetGreyExtension((char *)m_WizGreyIO->GetFileName());
  
  // Update the label of the UI
  UpdateMainLabel();

  // Get the largest image region
  GlobalState::RegionType roi = m_Driver->GetIRISImageData()->GetImageRegion();
  
  // Check if the region is real
  if (roi.GetNumberOfPixels() == 0) 
    {
    m_BtnStartSnake->deactivate();
    m_GlobalState->SetIsValidROI(false);
    } 
  else 
    {
    m_GlobalState->SetSegmentationROI(roi);
    m_GlobalState->SetIsValidROI(true);

    m_BtnResetROI->activate();
    m_BtnStartSnake->activate();
    }   

  // Update the polygon buttons
  OnPolygonStateUpdate(0);
  OnPolygonStateUpdate(1);
  OnPolygonStateUpdate(2);

  // Redraw the user interface
  RedrawWindows();
  m_WinMain->redraw();
}

void 
UserInterfaceLogic
::OnSegmentationImageUpdate()
{
  m_OutMessage->value("Loading Seg successful");

  m_SegmentationLoaded =1; //now have valid grey data

  // Re-init other UserInterfaceLogic components
  InitColorMap();

  // Re-Initialize the 2D windows
  for (unsigned int i=0; i<3; i++) 
      m_IRISWindow2D[i]->InitializeSlice(m_Driver->GetCurrentImageData());

  m_IRISWindow3D->ResetView();
  m_BtnMeshUpdate->activate();
  
  UpdateMainLabel();
  
  RedrawWindows();
  m_WinMain->redraw();
}

void 
UserInterfaceLogic
::OnSegmentationLabelsUpdate()
{
  InitColorMap();
  m_BtnMeshUpdate->activate();
  MakeSegTexturesCurrent();
  m_WinMain->redraw();        
  m_OutMessage->value("Loading Label file successful");

}

void 
UserInterfaceLogic
::OnSpeedImageUpdate()
{
  if(m_GlobalState->GetSpeedValid())
    {
    // Can flip between preprocessed and grey images
    m_GrpImageOptions->activate();

    // Choose to view the preprocessed image
    m_RadioSNAPViewPreprocessed->setonly();

    // Run the callback associated with that change
    OnViewPreprocessedSelect();
    
    // Allow preprocessing to be saved
    m_MenuSavePreprocessed->activate();

    // Allow progression to the next stage
    m_BtnAcceptPreprocessing->activate();
    }
  else
    {
    // Choose to view the grey image
    m_RadioSNAPViewOriginal->setonly();

    // Run the callback associated with that change
    OnViewPreprocessedSelect();
    
    // Can't flip between preprocessed and grey images
    m_GrpImageOptions->deactivate();

    // Disallow preprocessing to be saved
    m_MenuSavePreprocessed->deactivate();

    // Block progression to the next stage
    m_BtnAcceptPreprocessing->deactivate();
    }
}


/**
 * Load greyscale image using the wizard
 */
void 
UserInterfaceLogic
::OnMenuLoadGrey() 
{
  // Show the input wizard
  m_WizGreyIO->DisplayInputWizard();

  // If the load operation was successful, populate the data and GUI with the
  // new image
  if(m_WizGreyIO->IsImageLoaded()) 
    {
    // Send the image and RAI to the IRIS application driver
    m_Driver->UpdateIRISGreyImage(m_WizGreyIO->GetLoadedImage(),
                                  m_WizGreyIO->GetLoadedImageRAI());

    // Save the filename
    m_GlobalState->SetGreyFileName(m_WizGreyIO->GetFileName());

    // Update the user interface accordingly
    OnGreyImageUpdate();
    }
}

void 
UserInterfaceLogic
::OnLoadLabelsBrowseAction() 
{
  static const char *pattern = 
    "All Label Files (*.{label,lbl,lab,txt})";

  // Open a browse window and load a file
  char *chosenFile = fl_file_chooser("Select a File",pattern,NULL);
  if(chosenFile != NULL)
    m_InLoadLabelsFileName->value((const char*)chosenFile);
}

void 
UserInterfaceLogic
::OnLoadLabelsCancelAction() 
{
  // Close window and take no action
  m_WinLoadLabel->hide();
}

void 
UserInterfaceLogic
::OnLoadLabelsOkAction() 
{
  // Get label file
  const char* myfile = m_InLoadLabelsFileName->value();
  
  // Try reading file
  if (strlen(myfile) == 0) 
    {
    fl_alert("No filename entered -- cannot load");
    return;  
    }

  try 
    {
    // Read the labels from file
    m_Driver->ReadLabelDescriptionsFromTextFile(myfile);    
    
    // Update the user interface in response
    this->OnSegmentationLabelsUpdate();
    }
  catch (itk::ExceptionObject &exc) 
    {
    fl_alert("Error loading labels: %s",exc.GetDescription());
    return;
    }

  // Hide the window
  m_WinLoadLabel->hide();
}

void 
UserInterfaceLogic
::OnMenuLoadLabels() 
{
  m_WinLoadLabel->show();
}

void UserInterfaceLogic
::OnMenuSaveLabels() 
{

}

void UserInterfaceLogic
::OnMenuLoadSegmentation() 
{
  // Grey image should be loaded
  assert(m_Driver->GetCurrentImageData()->IsGreyLoaded());

  // Should not be in SNAP mode
  assert(!m_GlobalState->GetSNAPActive());
  
  // Set up the input wizard with the grey image
  m_WizSegmentationIO->SetGreyImage(
    m_Driver->GetCurrentImageData()->GetGrey()->GetImage());

  // Show the input wizard
  m_WizSegmentationIO->DisplayInputWizard();

  // If the load operation was successful, populate the data and GUI with the
  // new image
  if(m_WizSegmentationIO->IsImageLoaded()) 
    {
    // Send the image and RAI to the IRIS application driver
    m_Driver->UpdateIRISSegmentationImage(m_WizSegmentationIO->GetLoadedImage());

    // Save the segmentation file name
    m_GlobalState->SetSegmentationFileName(m_WizSegmentationIO->GetFileName());

    // Update the user interface accordingly
    OnSegmentationImageUpdate();
    }

  // Disconnect the input wizard from the grey image
  m_WizSegmentationIO->SetGreyImage(NULL);
}

void UserInterfaceLogic
::OnMenuSaveGreyROI() 
{
  // Better be in snap
  assert(m_GlobalState->GetSNAPActive());

  // Save the segmentation
  if(m_WizGreyIO->DisplaySaveWizard(
    m_Driver->GetCurrentImageData()->GetGrey()->GetImage()))
    {
    // Some silly feedback
    m_OutMessage->value("Saving segmentation file successful");    
    }
}

void UserInterfaceLogic
::OnMenuSaveSegmentation() 
{
  // Better have a segmentation image
  assert(m_Driver->GetIRISImageData()->IsSegmentationLoaded());

  // Save the segmentation
  if(m_WizSegmentationIO->DisplaySaveWizard(
    m_Driver->GetIRISImageData()->GetSegmentation()->GetImage()))
    {
    // Store the new filename
    m_GlobalState->SetSegmentationFileName(
      m_WizSegmentationIO->GetSaveFileName());

    // Some silly feedback
    m_OutMessage->value("Saving segmentation file successful");    
    }
}

void 
UserInterfaceLogic
::OnMenuLoadPreprocessed() 
{
  this->OnLoadPreprocessedImageAction();
}

void 
UserInterfaceLogic
::OnLoadPreprocessedImageAction() 
{
  // Preprocessing image can only be loaded in SNAP mode
  assert(m_GlobalState->GetSNAPActive());
  
  // Set up the input wizard with the grey image
  m_WizPreprocessingIO->SetGreyImage(
    m_Driver->GetCurrentImageData()->GetGrey()->GetImage());

  // Show the input wizard
  m_WizPreprocessingIO->DisplayInputWizard();

  // If the load operation was successful, populate the data and GUI with the
  // new image
  if(m_WizPreprocessingIO->IsImageLoaded()) 
    {
    // Update the application with the new speed image
    m_Driver->UpdateSNAPSpeedImage(
      m_WizPreprocessingIO->GetLoadedImage(),
      m_RadSnakeEdge->value() ? EDGE_SNAKE : IN_OUT_SNAKE);
    
    // Save the segmentation file name
    m_GlobalState->SetPreprocessingFileName(m_WizPreprocessingIO->GetFileName());

    // Update the user interface accordingly
    OnSpeedImageUpdate();
    }

  // Disconnect the input wizard from the grey image
  m_WizPreprocessingIO->SetGreyImage(NULL);
}

void 
UserInterfaceLogic
::OnMenuSavePreprocessed() 
{
  // Better have a speed image to save
  assert(m_GlobalState->GetSpeedValid());

  // Save the speed
  if(m_WizPreprocessingIO->DisplaySaveWizard(
    m_Driver->GetSNAPImageData()->GetSpeed()->GetImage()))
    {
    // Store the new filename
    m_GlobalState->SetPreprocessingFileName(
      m_WizPreprocessingIO->GetSaveFileName());

    // Some silly feedback
    m_OutMessage->value("Saving preprocessing file successful");    
    }
}


void UserInterfaceLogic
::OnMenuIntensityCurve() 
{
  // The image should be loaded before bringing up the curve
  assert(m_Driver->GetCurrentImageData()->IsGreyLoaded());

  // Update the curve in the UI dialog (is this necessary?)
  m_IntensityCurveUI->SetCurve(m_Driver->GetIntensityCurve());    
  m_IntensityCurveUI->SetImageWrapper(
    m_Driver->GetCurrentImageData()->GetGrey());

  // Show the window
  m_IntensityCurveUI->DisplayWindow();
}

void UserInterfaceLogic
::OnIntensityCurveUpdate() 
{
  // Update the image wrapper
  m_Driver->GetCurrentImageData()->GetGrey()->SetIntensityMapFunction(
    m_Driver->GetIntensityCurve());

  // Update the display
  RedrawWindows();
}


void
UserInterfaceLogic
::OnSNAPStepPreprocess()
{
  m_WizSegmentationPipeline->value(m_GrpSNAPStepPreprocess);
}

void
UserInterfaceLogic
::OnSNAPStepInitialize()
{
  m_WizSegmentationPipeline->value(m_GrpSNAPStepInitialize);
}

void
UserInterfaceLogic
::OnSNAPStepSegment()
{
  m_WizSegmentationPipeline->value(m_GrpSNAPStepSegment);
}

void
UserInterfaceLogic
::OnIRISLabelOpacityChange()
{
  m_GlobalState->SetSegmentationAlpha( (unsigned char) m_InIRISLabelOpacity->value());  
  this->RedrawWindows();
}

void
UserInterfaceLogic
::OnSNAPLabelOpacityChange()
{
  m_GlobalState->SetSegmentationAlpha( (unsigned char) m_InSNAPLabelOpacity->value());  
  this->RedrawWindows();
}

void
UserInterfaceLogic
::OnLaunchTutorialAction()
{
  m_WinHelp->show();
  m_BrsHelp->load("UserInterface/HTMLHelp/Tutorial.html");
}


// Splash screen functions
void
UserInterfaceLogic
::ShowSplashScreen()
{
  // Place the window in the center of display
  CenterChildWindowInParentWindow(m_WinSplash,m_WinMain);

  // Show a wait cursor
  fl_cursor(FL_CURSOR_WAIT,FL_FOREGROUND_COLOR, FL_BACKGROUND_COLOR);

  // Show the splash screen
  m_WinSplash->show();

  // Make FL update the screen
  Fl::check();

  // Save the time of the splash screen
  m_SplashScreenStartTime = clock();
}

void
UserInterfaceLogic
::HideSplashScreen()
{
  // Wait a second with the splash screen
  while(clock() - m_SplashScreenStartTime < CLOCKS_PER_SEC) {}
  
  // Hide the screen 
  m_WinSplash->hide();

  // Clear the cursor
  fl_cursor(FL_CURSOR_DEFAULT,FL_FOREGROUND_COLOR, FL_BACKGROUND_COLOR);
}

void
UserInterfaceLogic
::UpdateSplashScreen(const char *message)
{
  m_OutSplashMessage->label(message);
  Fl::check();

  // Save the time of the splash screen
  m_SplashScreenStartTime = clock();
}

void
UserInterfaceLogic
::CenterChildWindowInParentWindow(Fl_Window *childWindow,
                                  Fl_Window *parentWindow)
{
  int px = parentWindow->x() + (parentWindow->w() - childWindow->w()) / 2;
  int py = parentWindow->y() + (parentWindow->h() - childWindow->h()) / 2;
  childWindow->resize(px,py,childWindow->w(),childWindow->h());
}

void
UserInterfaceLogic
::OnResetView2DAction(unsigned int window)
{
  m_SliceCoordinator->ResetViewToFitInOneWindow(window);
  
  // Update the zoom level display
  OnZoomUpdate();
}

void
UserInterfaceLogic
::OnResetAllViews2DAction()
{
  m_SliceCoordinator->ResetViewToFitInAllWindows();
  
  // Update the zoom level display
  OnZoomUpdate();
}

void
UserInterfaceLogic
::OnLinkedZoomChange()
{
  if(m_ChkLinkedZoom->value() > 0)
    {
    m_SliceCoordinator->SetLinkedZoom(true);
    m_InZoomPercentage->activate();
    }
  else
    {
    m_SliceCoordinator->SetLinkedZoom(false);
    m_InZoomPercentage->deactivate();
    }
  
  // Update the zoom level display
  OnZoomUpdate();
}

void
UserInterfaceLogic
::OnZoomPercentageChange()
{
  float zoom = m_InZoomPercentage->value();
  m_SliceCoordinator->SetZoomFactorAllWindows(zoom / 100.0f);  
}

void 
UserInterfaceLogic
::OnZoomUpdate()
{
  // This method should be called whenever the zoom changes
  if(m_SliceCoordinator->GetLinkedZoom())
    {
    // Get the zoom from the first window and display it on the screen
    m_InZoomPercentage->value(m_SliceCoordinator->GetCommonZoom() * 100);
    }

  else
    {
    // Otherwise, clear the display
    m_InZoomPercentage->value(0);
    }
}


/*
void UserInterfaceLogic
::OnCursorPositionUpdate()
{
// Tell the application that the cursor position has changed
m_Driver->SetCursorPosition(m_GlobalState)

// Request the update of the windows

// Recalculate the probe position
}
*/

/*
 *Log: UserInterfaceLogic.cxx
 *Revision 1.7  2003/10/02 18:43:47  pauly
 *FIX: Fixed crashes with using vtkContourFilter
 *
 *Revision 1.6  2003/10/02 14:55:52  pauly
 *ENH: Development during the September code freeze
 *
 *Revision 1.5  2003/09/16 16:10:17  pauly
 *FIX: No more Internal compiler errors on VC
 *FIX: Intensity curve no longer crashes
 *ENH: Histogram display on intensity curve window
 *
 *Revision 1.4  2003/09/15 19:06:58  pauly
 *FIX: Trying to get last changes to compile
 *
 *Revision 1.3  2003/09/15 17:32:19  pauly
 *ENH: Removed ImageWrapperImplementation classes
 *
 *Revision 1.2  2003/09/13 15:18:01  pauly
 *FIX: Got SNAP to work properly with different image orientations
 *
 *Revision 1.1  2003/09/11 13:51:01  pauly
 *FIX: Enabled loading of images with different orientations
 *ENH: Implemented image save and load operations
 *
 *Revision 1.5  2003/08/28 22:58:30  pauly
 *FIX: Erratic scrollbar behavior
 *
 *Revision 1.4  2003/08/28 14:37:09  pauly
 *FIX: Clean 'unused parameter' and 'static keyword' warnings in gcc.
 *FIX: Label editor repaired
 *
 *Revision 1.3  2003/08/27 14:03:22  pauly
 *FIX: Made sure that -Wall option in gcc generates 0 warnings.
 *FIX: Removed 'comment within comment' problem in the cvs log.
 *
 *Revision 1.2  2003/08/27 04:57:46  pauly
 *FIX: A large number of bugs has been fixed for 1.4 release
 *
 *Revision 1.1  2003/07/12 04:46:50  pauly
 *Initial checkin of the SNAP application into the InsightApplications tree.
 *
 *Revision 1.1  2003/07/11 23:33:57  pauly
 **** empty log message ***
 *
 *Revision 1.20  2003/07/10 14:30:26  pauly
 *Integrated ITK into SNAP level set segmentation
 *
 *Revision 1.19  2003/07/01 16:53:59  pauly
 **** empty log message ***
 *
 *Revision 1.18  2003/06/23 23:59:32  pauly
 *Command line argument parsing
 *
 *Revision 1.17  2003/06/14 22:42:06  pauly
 *Several changes.  Started working on implementing the level set function
 *in ITK.
 *
 *Revision 1.16  2003/06/08 23:27:56  pauly
 *Changed variable names using combination of ctags, egrep, and perl.
 *
 *Revision 1.15  2003/06/08 16:11:42  pauly
 *User interface changes
 *Automatic mesh updating in SNAP mode
 *
 *Revision 1.14  2003/05/22 17:36:19  pauly
 *Edge preprocessing settings
 *
 *Revision 1.13  2003/05/17 21:39:30  pauly
 *Auto-update for in/out preprocessing
 *
 *Revision 1.12  2003/05/14 18:33:58  pauly
 *SNAP Component is working. Double thresholds have been enabled.  Many other changes.
 *
 *Revision 1.11  2003/05/12 02:51:08  pauly
 *Got code to compile on UNIX
 *
 *Revision 1.10  2003/05/08 21:59:05  pauly
 *SNAP is almost working
 *
 *Revision 1.9  2003/05/07 19:14:46  pauly
 *More progress on getting old segmentation working in the new SNAP.  Almost there, region of interest and bubbles are working.
 *
 *Revision 1.8  2003/05/05 12:30:18  pauly
 **** empty log message ***
 *
 *Revision 1.7  2003/04/29 14:01:42  pauly
 *Charlotte Trip
 *
 *Revision 1.6  2003/04/25 02:58:29  pauly
 *New window2d model with InteractionModes
 *
 *Revision 1.5  2003/04/23 20:36:23  pauly
 **** empty log message ***
 *
 *Revision 1.4  2003/04/23 06:05:18  pauly
 **** empty log message ***
 *
 *Revision 1.3  2003/04/18 17:32:18  pauly
 **** empty log message ***
 *
 *Revision 1.2  2003/04/18 00:25:37  pauly
 **** empty log message ***
 *
 *Revision 1.1  2003/04/16 05:04:17  pauly
 *Incorporated intensity modification into the snap pipeline
 *New IRISApplication
 *Random goodies
 *
 *Revision 1.2  2003/04/01 18:20:56  pauly
 **** empty log message ***
 *
 *Revision 1.1  2003/03/07 19:29:47  pauly
 *Initial checkin
 *
 *Revision 1.1.1.1  2002/12/10 01:35:36  pauly
 *Started the project repository
 *
 *
 *Revision 1.63  2002/05/08 17:32:57  moon
 *I made some changes Guido wanted in the GUI, including removing
 *Turello/Sapiro/Schlegel options (I only hid them, the code is all still there), changing a bunch of the ranges, etc. of the snake parameters.
 *
 *Revision 1.62  2002/04/28 17:29:43  scheuerm
 *Added some documentation
 *
 *Revision 1.61  2002/04/27 18:30:03  moon
 *Finished commenting
 *
 *Revision 1.60  2002/04/27 17:48:33  bobkov
 *Added comments
 *
 *Revision 1.59  2002/04/27 00:08:27  talbert
 *Final commenting run through . . . no functional changes.
 *
 *Revision 1.58  2002/04/26 17:37:12  moon
 *Fixed callback on save preproc dialog cancel button.
 *Fixed bubble browser output.  Position was zero-based, which didn't match the 2D
 *window slice numbers (1 based), so I changed the bubble positions to be cursor
 *position +1.
 *Disallowed starting snake window if current label in not visible.
 *Put in Apply+ button in threshold dialog, which changes seg overlay to be an
 *overlay of the positive voxels in the preproc data (a zero-level visualization).
 *Added more m_OutMessage and m_OutMessage messages.
 *
 *Revision 1.57  2002/04/25 14:13:13  moon
 *Enabled render options in snake window.
 *Changed snake params dialog slider (messed one up last time)
 *Hide r_ params in snake params dialog with in/out snake (they don't apply)
 *Calling segment3D with clear color is not allowed (error dialog)
 *
 *Revision 1.56  2002/04/24 19:50:22  moon
 *Pulled LoadGreyFileCallback out of GUI into UserInterfaceLogic, made modifications due
 *to change in ROI semantics.  Before, the ROI was from ul to lr-1, which is a bad
 *decision.  I changed everything to work with a ROI that is inclusive, meaning
 *that all voxels from ul through lr inclusive are part of the ROI. This involved
 *a lot of small changes to a lot of files.
 *
 *Revision 1.55  2002/04/24 17:10:33  bobkov
 *Added some changes to OnSnakeStartAction(),
 *OnAcceptInitializationAction() and
 *OnRestartInitializationAction()
 *so that ClearScreen() method is called on
 *m_IRISWindow3D and m_SNAPWindow3D to clear the glLists and
 *the previous segmentation is not shown in the 3D window
 *
 *Revision 1.54  2002/04/24 14:54:32  moon
 *Changed the ranges of some of the snake parameters after talking with Guido.
 *Put in a line to update mesh when the update continuously checkbox is checked.
 *
 *Revision 1.53  2002/04/24 14:13:26  moon
 *Implemented separate brightness/contrast settings for grey/preproc data
 *
 *Revision 1.52  2002/04/24 01:05:00  talbert
 *Changed IRIS2000 labels to SnAP.
 *
 *Revision 1.51  2002/04/23 21:56:50  moon
 *small bug fix with the snake params and the global state.  wasn't getting
 *the sapiro/turello/etc. option from the dialog to put into the global state.
 *
 *Revision 1.50  2002/04/23 03:19:40  talbert
 *Made some changes so that the load preproc menu option is unuseable once
 *the snake starts.
 *
 *Revision 1.49  2002/04/22 21:54:39  moon
 *Closed dialogs when accept/restart initialization is pressed, or snake type is
 *switched.
 *
 *Revision 1.48  2002/04/22 21:24:20  talbert
 *Small changes so that error messages for preprocessing loading appeared in
 *the correct status bar.
 *
 *Revision 1.47  2002/04/20 21:56:47  talbert
 *Made it impossible to save preprocessed data when it doesn't make sense
 *(if no preprocessing has been done since the last preproc load or since
 *the snake win opened).  Moved some checks for data type validity out of
 *the callbacks and into the Vox and SnakeVoxData classes where they belong.
 *
 *Revision 1.46  2002/04/19 23:03:59  moon
 *Changed more stuff to get the snake params state synched with the global state.
 *Changed the range of ground in snake params dialog.
 *Removed the use_del_g stuff, since it's really not necessary, I found out.
 *
 *Revision 1.45  2002/04/19 20:34:58  moon
 *Made preproc dialogs check global state and only preproc if parameters have changed.
 *So no if you hit apply, then ok, it doesn't re process on the ok.
 *
 *Revision 1.44  2002/04/18 21:36:51  scheuerm
 *Added documentation for my recent changes.
 *Fixed inverted display of edge preprocessing.
 *
 *Revision 1.43  2002/04/18 21:14:03  moon
 *I had changed the Cancel buttons to be Close on the Filter dialogs, and I changed
 *the names of the callbacks in GUI, but not in UserInterfaceLogic.  So I just hooked them
 *up so the dialogs get closed.
 *
 *Revision 1.42  2002/04/18 21:04:51  moon
 *Changed the IRIS window ROI stuff.  Now the ROI is always valid if an image is
 *loaded, but there is a toggle to show it or not.  This will work better with
 *Konstantin's addition of being able to drag the roi box.
 *
 *I also changed a bunch of areas where I was calling InitializeSlice for the 2D windows,
 *when this is not at all what I should have done.  Now those spots call
 *MakeSegTextureCurrent, or MakeGreyTextureCurrent.  This means that the view is not
 *reset every time the snake steps, the preproc/orig radio buttons are changed, etc.
 *
 *Revision 1.41  2002/04/16 18:54:32  moon
 *minor bug with not stopping snake when play is pushed, and then other
 *buttons are pushed.  Also added a function that can be called when the user
 *clicks the "X" on a window, but it's not what we want, I don't think.  The
 *problem is if the user clicks the "X" on the snake window when a "non modal"
 *dialog is up, all the windows close, but the program doesn't quit.  I think
 *it's a bug in FLTK, but I can't figure out how to solve it.
 *
 *Revision 1.40  2002/04/16 14:44:49  moon
 *Changed bubbles to be in world coordinates instead of image coordinates.
 *
 *Revision 1.39  2002/04/16 13:07:56  moon
 *Added tooltips to some widgets, made minor changes to enabling/disabling of
 *widgets, clearing 3D window when initialization is restarted in snake window,
 *changed kappa in edge preproc dialog to be [0..1] range instead of [0..3]
 *
 *Revision 1.38  2002/04/14 22:02:54  scheuerm
 *Changed loading dialog for preprocessed image data. Code restructuring
 *along the way: Most important is addition of
 *SnakeVoxDataClass::ReadRawPreprocData()
 *
 *Revision 1.37  2002/04/13 17:43:48  moon
 *Added some initslice calls to Win2Ds, so the redraw problem comming back
 *from snake to iris window (where the whole 2D window is yellow) would go away.
 *
 *Revision 1.36  2002/04/13 16:20:08  moon
 *Just put in a bunch of debug printouts.  They'll have to come out eventually.
 *
 *Revision 1.35  2002/04/10 21:20:16  moon
 *just added debug comments.
 *
 *Revision 1.34  2002/04/10 20:19:40  moon
 *got play and stop vcr buttons to work.
 *put in lots of comments.
 *
 *Revision 1.33  2002/04/10 14:45:03  scheuerm
 *Added documentation to the methods I worked on.
 *
 *Revision 1.32  2002/04/09 21:56:42  bobkov
 *
 *modified Step button callback to display snake in 3d window
 *
 *Revision 1.31  2002/04/09 19:32:22  talbert
 *Added comments to the save and load preprocessed functions.  Checked that
 *only float files entered as preprocessed.  Made some small cosmetic
 *changes:  loading a file switches to preproc view and sets snake mode.
 *
 *Revision 1.30  2002/04/09 18:59:33  moon
 *Put in dialog to change snake parameters.  Also implemented Rewind button, which
 *now restarts the snake.  It seems for now that changing snake parameters restarts
 *the snake.  I don't know if this is the way it has to be, or I just did something
 *wrong in snakewrapper.  I'll have to check with Sean.
 *
 *Revision 1.29  2002/04/09 17:59:37  talbert
 *Made changes to LoadPreprocessedCallback which allowed edge detection
 *preproc data to be loaded correctly.
 *
 *Revision 1.28  2002/04/09 03:48:51  talbert
 *Changed some functionality in the LoadPreprocessedCallback() so that it
 *would work with floating point data being loaded.  Most of the stuff
 *is uncommented, hackish, and limited in its testing beyond verification
 *that it displays on the screen with the right values.  These changes
 *will have to be cleaned up.
 *
 *Revision 1.27  2002/04/08 13:32:35  talbert
 *Added a preprocessed save dialog box as well as a save preprocessed menu
 *option in the snake window.  Added the code necessary to implement the
 *GUI side of saving.
 *
 *Revision 1.26  2002/04/07 02:22:49  scheuerm
 *Improved handling of OK and Apply buttons in preprocessing dialogs.
 *
 *Revision 1.23  2002/04/05 03:42:29  scheuerm
 *Thresholding sort of works. Steepness needs to be made configurable.
 *
 *Revision 1.21  2002/04/04 15:30:08  moon
 *Put in code to get StepSize choice box filled with values and working.
 *AcceptSegment button callback puts snake seg data into full_data (IRIS)
 *Fixed a couple more UI cosmetic things.
 *
 *Revision 1.20  2002/04/03 22:12:07  moon
 *Added color chip, image probe, seg probe to snake window, although seg probe
 *maybe shouldn't be there.  added update continuously checkbox to 3Dwindow.
 *changes accept/restart to be on top of each other, and one is shown at a time,
 *which I think is more intuitive.
 *changed snake iteration field to be text output.  added callback for step size
 *choice.
 *
 *Revision 1.19  2002/04/02 23:51:17  scheuerm
 *Gradient magnitude preprocessing is implemented. Stupid, stupid VTK.
 *Adjusted the range and resolution of the sigma slider. Apply button
 *still doesn't do anything but I think we don't need it.
 *
 *Revision 1.18  2002/04/02 15:12:43  moon
 *Put code in the step vcr button.  Now the snake can be "stepped"
 *
 *Revision 1.17  2002/04/01 22:29:54  moon
 *Modified OnAcceptInitializationAction, added functionality to
 *OnRestartInitializationAction
 *
 *Revision 1.16  2002/03/29 20:17:25  scheuerm
 *Implemented remapping of preprocessed data to (-1,1). The mapping can
 *be changed by altering the parameters to RemapPreprocData(...) in
 *LoadPreprocessedDataCallback() (UserInterfaceLogic.cpp).
 *
 *Revision 1.15  2002/03/29 03:33:29  scheuerm
 *Loaded preprocessed data is now converted to float. No remapping yet.
 *Stupid VTK. Added vtkImageDeepCopyFloat which copies the region of
 *interest out of a gray image and converts it to float as it goes.
 *
 *Revision 1.14  2002/03/27 17:59:40  moon
 *changed a couple things.  nothing big. a callback in .fl was bool return type
 *which didn't compile in windows. this is the version I think will work for a
 *demo for Kye
 *
 *Revision 1.13  2002/03/27 17:05:04  talbert
 *Made changes necessary to compile in Windows 2000 using Microsoft Visual C++ 6.0.
 *GUI.cpp - needed to return something from function LoadPreprocessedCallback()
 *UserInterfaceLogic.cpp - moved definitions of for loop control variables outside of loop for
 *scope reasons.
 *SnakeWrapper.cpp - changed outdt1->data to *outdt1 and outdt2->data to *outdt because
 *these variables are float, not structures.  Also changed a line using snprintf to
 *sprintf because snprintf is a GNU extension.
 *Added the files snake32.dsp and snake32.dsw for compiling in Windows 2000.
 *
 *Revision 1.12  2002/03/27 15:04:26  moon
 *Changed a bunch of stuff so that the state was basically reset when the snake
 *window is hidden (accept or cancel segmentation), and then opened again.  for
 *example, the bubbles browser needed to be emptied, the active/inactive groups
 *needed to be set to the defaults again, the radio button for the preproc
 *data needed to be turned off (so original data is shown), etc.
 *
 *Added code to the acceptinitialization button that converts bubble information
 *into binary snake initialization image, and previous segmentation info of the
 *same label should also be preserved (i.e. segmentation info that comes from
 *IRIS can be used for the initialization as well as bubbles). The snake is
 *initialized, and the controls are activated.
 *
 *Still need to code the resetinitialization so that the bubble stuff, etc. is re-
 *enabled.
 *
 *None of the vcr buttons do anything, still.
 *
 *Revision 1.11  2002/03/26 19:22:14  moon
 *I don't think I really changed anything, but I had updated, and it tried to "merge" versions with and without ^M endline characters
 *
 *Revision 1.10  2002/03/26 18:16:32  scheuerm
 *Added loading and display of preprocessed data:
 *- added vtkImageDeepCopy function
 *- added flags indicating which dataset to display in GlobalState
 *- added flag indicating whether to load gray or preprocessed data
 *  in the GUI class
 *
 *Revision 1.9  2002/03/25 02:15:57  scheuerm
 *Added loading of preprocessed data. It isn't being converted
 *to floats yet. It's not possible to actually display the data
 *right now.
 *
 *Revision 1.8  2002/03/24 19:27:46  talbert
 *Added callback the preprocess button to show dialog boxes for filtering.  Added callbacks for buttons in filtering dialog boxes.  Modified the AddBubbles callback so that the newest bubble is selected in the Bubble Browser.  m_OutAboutCompiled and ran to verify that new bubbles are selected and that the dialogs appear over the
 *3d window.  talbert s f
 *
 *Revision 1.7  2002/03/22 16:44:16  moon
 *added OpenGL display of bubbles in Window2D_s::draw
 *
 *Revision 1.6  2002/03/21 15:45:46  bobkov
 *implemented callbacks for buttons AddBubble and RemoveBubble, implemented callbacks for Radius slider and ActiveBubble browser, created methods getBubbles and getNumberOfBubbles   e
 *
 *Revision 1.5  2002/03/19 19:35:32  moon
 *added snakewrapper to makefile so it gets compiled. started putting in callback,
 *etc. for snake vcr buttons.  added snake object to IrisGlobals, instantiated in Main
 *
 *Revision 1.4  2002/03/19 17:47:10  moon
 *added some code to disable widgets, make the radio buttons work, etc. in the snake window.  fixed the quit callback from the snake window to work (crashed before)
 *changed the [accept/restart]bubble_button widgets to be acceptinitialization_button and added callbacks (empty).
 *
 *Revision 1.3  2002/03/08 14:06:29  moon
 *Added Header and Log tags to all files
 **/
