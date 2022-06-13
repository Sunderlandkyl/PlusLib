/*=Plus=header=begin======================================================
    Program: Plus
    Copyright (c) UBC Biomedical Signal and Image Computing Laboratory. All rights reserved.
=========================================================Plus=header=end*/

#ifndef _VTKPLUSTOBIIINTERACTIONSDKTRACKER_H
#define _VTKPLUSTOBIIINTERACTIONSDKTRACKER_H

// Local Includes
#include "vtkPlusConfig.h"
#include "vtkPlusDataCollectionExport.h"
#include "vtkPlusDevice.h"
#include "vtkPlusDataSource.h"
#include "vtkPlusUsDevice.h"

/*!
\class vtkPlusTobiiInteractionSDKTracker
\brief Interface for Tobii eye trackers using the Interaction SDK.
Requires PLUS_USE_TOBII_INTERACTION option in CMake.
 \ingroup PlusLibDataCollection
*/
class vtkPlusDataCollectionExport vtkPlusTobiiInteractionSDKTracker : public vtkPlusDevice
{
public:
  vtkTypeMacro(vtkPlusTobiiInteractionSDKTracker, vtkPlusDevice);
  /*! This is a singleton pattern New. There will only be ONE
  reference to a vtkPlusTobiiInteractionSDKTracker object per process. Clients that
  call this must call Delete on the object so that the reference
  counting will work. The single instance will be unreferenced
  when the program exits. */
  static vtkPlusTobiiInteractionSDKTracker* New();

  /*! return the singleton instance with no reference counting */
  static vtkPlusTobiiInteractionSDKTracker* GetInstance();

  void PrintSelf(ostream& os, vtkIndent indent) VTK_OVERRIDE;

  /*!
  Hardware device SDK version. 
  */
  std::string GetSdkVersion() override;

  /*! Read configuration from xml data */
  PlusStatus ReadConfiguration(vtkXMLDataElement* config) override;

  /*! Write configuration to xml data */
  PlusStatus WriteConfiguration(vtkXMLDataElement* config) override;

  bool IsTracker() const override { return true; }

protected:
  vtkPlusTobiiInteractionSDKTracker();
  ~vtkPlusTobiiInteractionSDKTracker();

  PlusStatus InternalConnect() override;
  PlusStatus InternalDisconnect() override;
  PlusStatus InternalUpdate() override;

protected:
  static vtkPlusTobiiInteractionSDKTracker* instance;

  class vtkInternal;
  vtkInternal* Internal;
};

#endif //_VTKPLUSTOBIIINTERACTIONSDKTRACKER_H
