/*=Plus=header=begin======================================================
    Program: Plus
    Copyright (c) UBC Biomedical Signal and Image Computing Laboratory. All rights reserved.
    See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusConfigure.h"
#include "vtkPlusChannel.h"
#include "vtkPlusDataSource.h"
#include "vtkPlusTobiiInteractionSDKTracker.h"

#include <InteractionLib.h>
#include <misc/InteractionLibPtr.h>

// VTK includes
#include <vtkMatrix4x4.h>
#include <vtkTransform.h>

//-------------------------------------------------------------------------------------------------
// vtkInternal
//-------------------------------------------------------------------------------------------------

class vtkPlusTobiiInteractionSDKTracker::vtkInternal
{
public:

  vtkInternal(vtkPlusTobiiInteractionSDKTracker* ext);

  virtual ~vtkInternal()
  {
  }

protected:
  friend class vtkPlusTobiiInteractionSDKTracker;

  IL::UniqueInteractionLibPtr InteractionLib;

  vtkPlusDataSource* GazeTool;
  vtkNew<vtkMatrix4x4> GazeToTracker;
  ToolStatus GazePointDataStatus;

  vtkPlusDataSource* LeftGazeOriginTool;
  vtkNew<vtkMatrix4x4> LeftGazeOriginToTracker;
  ToolStatus LeftGazeOriginStatus;

  vtkPlusDataSource* RightGazeOriginTool;
  vtkNew<vtkMatrix4x4> RightGazeOriginToTracker;
  ToolStatus RightGazeOriginStatus;

  vtkPlusDataSource* HeadTool;
  vtkNew<vtkMatrix4x4> HeadToTracker;
  ToolStatus HeadPoseStatus;

  vtkPlusDataSource* InteractorFieldData;
  vtkPlusDataSource* PresenceFieldData;

  enum SizeModeType
  {
    SIZE_MODE_ABSOLUTE,
    SIZE_MODE_RELATIVE
  };

  struct InteractorData
  {
    IL::InteractorId ID;
    std::string Name;
    double Origin[2] = { 0.0, 0.0 };
    double Size[2] = { 0.0, 0.0 };
    double Z = 0.0;
    SizeModeType SizeMode = SIZE_MODE_ABSOLUTE;
  };
  std::map<IL::InteractorId, InteractorData> Interactors;

  double ScreenSize[2];
  double ScreenOffset[2];
  std::string NoFocusName;

  IL::InteractorId FocusedInteractor;
  IL::InteractorId HasFocus;
  IL::Presence Presence;

protected:
  static void OnGazePointDataCallback(IL::GazePointData evt, void* context);
  static void OnGazeOriginDataCallback(IL::GazeOriginData evt, void* context);
  static void OnHeadPoseDataCallback(IL::HeadPoseData evt, void* context);
  static void OnGazeFocusEventCallback(IL::GazeFocusEvent evt, void* context);
  static void OnPresenceDataCallback(IL::PresenceData evt, void* context);

private:
  vtkPlusTobiiInteractionSDKTracker* External;
};


//----------------------------------------------------------------------------
vtkPlusTobiiInteractionSDKTracker::vtkInternal::vtkInternal(vtkPlusTobiiInteractionSDKTracker* ext)
  : External(ext)
  , InteractionLib(IL::UniqueInteractionLibPtr(IL::CreateInteractionLib(IL::FieldOfUse::Interactive)))
  , GazeTool(NULL)
  , GazePointDataStatus(TOOL_OUT_OF_VIEW)
  , LeftGazeOriginTool(NULL)
  , LeftGazeOriginStatus(TOOL_OUT_OF_VIEW)
  , RightGazeOriginTool(NULL)
  , RightGazeOriginStatus(TOOL_OUT_OF_VIEW)
  , HeadTool(NULL)
  , HeadPoseStatus(TOOL_OUT_OF_VIEW)
  , InteractorFieldData(NULL)
  , PresenceFieldData(NULL)
  , ScreenSize{ -1.0, -1.0 }
  , ScreenOffset{ 0.0, 0.0 }
  , NoFocusName("None")
  , FocusedInteractor(0)
  , HasFocus(false)
  , Presence(IL::Presence::Unknown)
{
}

//----------------------------------------------------------------------------
void vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnGazePointDataCallback(IL::GazePointData evt, void* context)
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: OnGazePointDataCallback");

  vtkPlusTobiiInteractionSDKTracker::vtkInternal* self = static_cast<vtkPlusTobiiInteractionSDKTracker::vtkInternal*>(context);

  self->GazeToTracker->Identity();
  self->GazeToTracker->SetElement(0, 3, evt.x);
  self->GazeToTracker->SetElement(1, 3, evt.y);
  self->GazePointDataStatus = evt.validity == IL_Validity_Valid ? TOOL_OK : TOOL_OUT_OF_VIEW;
}

//----------------------------------------------------------------------------
void vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnGazeOriginDataCallback(IL::GazeOriginData evt, void* context)
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: OnGazeOriginDataCallback");

  vtkPlusTobiiInteractionSDKTracker::vtkInternal* self = static_cast<vtkPlusTobiiInteractionSDKTracker::vtkInternal*>(context);

  if (self->LeftGazeOriginTool)
  {
    self->LeftGazeOriginToTracker->Identity();
    self->LeftGazeOriginToTracker->SetElement(0, 3, evt.left_xyz[0]);
    self->LeftGazeOriginToTracker->SetElement(1, 3, -evt.left_xyz[2]);
    self->LeftGazeOriginToTracker->SetElement(2, 3, evt.left_xyz[1]);
    self->LeftGazeOriginStatus = evt.leftValidity == IL_Validity::IL_Validity_Valid ? TOOL_OK : TOOL_OUT_OF_VIEW;
  }

  if (self->RightGazeOriginTool)
  {
    self->RightGazeOriginToTracker->Identity();
    self->RightGazeOriginToTracker->SetElement(0, 3, evt.right_xyz[0]);
    self->RightGazeOriginToTracker->SetElement(1, 3, -evt.right_xyz[2]);
    self->RightGazeOriginToTracker->SetElement(2, 3, evt.right_xyz[1]);
    self->RightGazeOriginStatus = evt.rightValidity == IL_Validity::IL_Validity_Valid ? TOOL_OK : TOOL_INVALID;
  }

}

//----------------------------------------------------------------------------
void vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnHeadPoseDataCallback(IL::HeadPoseData evt, void* context)
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: OnHeadPoseDataCallback");

  vtkPlusTobiiInteractionSDKTracker::vtkInternal* self = static_cast<vtkPlusTobiiInteractionSDKTracker::vtkInternal*>(context);

  vtkNew<vtkTransform> headTransform;
  headTransform->PostMultiply();
  headTransform->RotateX(vtkMath::DegreesFromRadians(evt.rotation_xyz[0]));
  headTransform->RotateY(vtkMath::DegreesFromRadians(-evt.rotation_xyz[2]));
  headTransform->RotateZ(vtkMath::DegreesFromRadians(evt.rotation_xyz[1]));
  headTransform->Translate(evt.position_xyz[0], -evt.position_xyz[2], evt.position_xyz[1]);
  self->HeadToTracker->DeepCopy(headTransform->GetMatrix());

  bool toolValid =
    evt.rotation_validity_xyz[0] == IL_Validity::IL_Validity_Valid &&
    evt.rotation_validity_xyz[1] == IL_Validity::IL_Validity_Valid &&
    evt.rotation_validity_xyz[2] == IL_Validity::IL_Validity_Valid;
  self->HeadPoseStatus = toolValid ? TOOL_OK : TOOL_OUT_OF_VIEW;
}

//----------------------------------------------------------------------------
void vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnGazeFocusEventCallback(IL::GazeFocusEvent evt, void* context)
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: OnGazeFocusEventCallback");

  vtkPlusTobiiInteractionSDKTracker::vtkInternal* self = static_cast<vtkPlusTobiiInteractionSDKTracker::vtkInternal*>(context);
  self->FocusedInteractor = evt.id;
  self->HasFocus = evt.hasFocus;
}

//----------------------------------------------------------------------------
void vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnPresenceDataCallback(IL::PresenceData evt, void* context)
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: OnPresenceDataCallback");

  vtkPlusTobiiInteractionSDKTracker::vtkInternal* self = static_cast<vtkPlusTobiiInteractionSDKTracker::vtkInternal*>(context);
  self->Presence = static_cast<IL::Presence>(evt.presence);
}


//----------------------------------------------------------------------------
vtkPlusTobiiInteractionSDKTracker* vtkPlusTobiiInteractionSDKTracker::instance;

//----------------------------------------------------------------------------
vtkPlusTobiiInteractionSDKTracker* vtkPlusTobiiInteractionSDKTracker::New()
{
  if (instance == NULL)
  {
    instance = new vtkPlusTobiiInteractionSDKTracker();
  }
  return instance;
}

//----------------------------------------------------------------------------
vtkPlusTobiiInteractionSDKTracker::vtkPlusTobiiInteractionSDKTracker()
{
  LOG_TRACE("vtkPlusTobiiInteractionSDKTracker: Constructor");

  this->Internal = new vtkPlusTobiiInteractionSDKTracker::vtkInternal(this);

  this->StartThreadForInternalUpdates = true;
  this->RequirePortNameInDeviceSetConfiguration = true;

  instance = this;
}

//----------------------------------------------------------------------------
vtkPlusTobiiInteractionSDKTracker::~vtkPlusTobiiInteractionSDKTracker()
{
  delete this->Internal;
  this->Internal = NULL;
  this->instance = NULL;
}

//----------------------------------------------------------------------------
vtkPlusTobiiInteractionSDKTracker* vtkPlusTobiiInteractionSDKTracker::GetInstance()
{
  LOG_TRACE("vtkPlusTobiiInteractionSDKTracker: GetInstance()");
  if (instance != NULL)
  {
    return instance;
  }

  else
  {
    LOG_ERROR("Instance is null, creating new instance");
    instance = new vtkPlusTobiiInteractionSDKTracker();
    return instance;
  }
}

//----------------------------------------------------------------------------
void vtkPlusTobiiInteractionSDKTracker::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusTobiiInteractionSDKTracker::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusTobiiInteractionSDKTracker::ReadConfiguration");
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);
  XML_READ_VECTOR_ATTRIBUTE_NONMEMBER_OPTIONAL(double, 2, ScreenSize, this->Internal->ScreenSize, deviceConfig);
  XML_READ_VECTOR_ATTRIBUTE_NONMEMBER_OPTIONAL(double, 2, ScreenOffset, this->Internal->ScreenOffset, deviceConfig);
  XML_READ_STRING_ATTRIBUTE_NONMEMBER_OPTIONAL(NoFocusName, this->Internal->NoFocusName, deviceConfig);

  // Read interactor regions from the config file.
  XML_FIND_NESTED_ELEMENT_OPTIONAL(interactors, deviceConfig, "Interactors");
  for (int i = 0; i < interactors->GetNumberOfNestedElements(); ++i)
  {
    vtkXMLDataElement* interactor = interactors->GetNestedElement(i);
    if (strcmp(interactor->GetName(), "Interactor") != 0)
    {
      continue;
    }
    vtkInternal::InteractorData interactorData;
    interactorData.ID = i;
    XML_READ_STRING_ATTRIBUTE_NONMEMBER_REQUIRED(Name, interactorData.Name, interactor);
    if (interactorData.Name == "None")
    {
      LOG_ERROR("Invalid interactor name \"None\" is reserved");
      return PLUS_FAIL;
    }

    XML_READ_VECTOR_ATTRIBUTE_NONMEMBER_REQUIRED(double, 2, Origin, interactorData.Origin, interactor);
    XML_READ_VECTOR_ATTRIBUTE_NONMEMBER_REQUIRED(double, 2, Size, interactorData.Size, interactor);
    XML_READ_SCALAR_ATTRIBUTE_NONMEMBER_OPTIONAL(double, Z, interactorData.Z, interactor);
    XML_READ_ENUM2_ATTRIBUTE_NONMEMBER_OPTIONAL(SizeMode, interactorData.SizeMode, interactor, "ABSOLUTE", vtkInternal::SIZE_MODE_ABSOLUTE, "RELATIVE", vtkInternal::SIZE_MODE_RELATIVE);
    this->Internal->Interactors[interactorData.ID] = interactorData;
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusTobiiInteractionSDKTracker::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  LOG_TRACE("vtkPlusTobiiInteractionSDKTracker::WriteConfiguration");
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusTobiiInteractionSDKTracker::NotifyConfigured()
{
  LOG_TRACE("vtkPlusTobiiInteractionSDKTracker::NotifyConfigured");
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusTobiiInteractionSDKTracker::Probe()
{
  LOG_TRACE("vtkPlusTobiiInteractionSDKTracker: Probe");
  return PLUS_SUCCESS;
};

//----------------------------------------------------------------------------
std::string vtkPlusTobiiInteractionSDKTracker::vtkPlusTobiiInteractionSDKTracker::GetSdkVersion()
{
  std::ostringstream version;
  version << "Sdk version not available" << "\n";
  return version.str();
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusTobiiInteractionSDKTracker::InternalConnect()
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: InternalConnect");

  // Screen size updates
  if (this->Internal->ScreenSize[0] >= 0 && this->Internal->ScreenSize[1] >= 0)
  {
    this->Internal->InteractionLib->CoordinateTransformAddOrUpdateDisplayArea(
      this->Internal->ScreenSize[0], this->Internal->ScreenSize[1]);
  }
  this->Internal->InteractionLib->CoordinateTransformSetOriginOffset(
    this->Internal->ScreenOffset[0], this->Internal->ScreenOffset[1]);


  // Add interactors
  if (this->Internal->InteractionLib->BeginInteractorUpdates() != IL::Result::Ok)
  {
    LOG_ERROR("Could not begin interactor update");
  }
  this->Internal->InteractionLib->ClearInteractors();

  for (auto interactorDataIt : this->Internal->Interactors)
  {
    vtkInternal::InteractorData interactorData = interactorDataIt.second;
    IL::Rectangle interactorRectangle = {
      interactorData.Origin[0],
      interactorData.Origin[1],
      interactorData.Size[0],
      interactorData.Size[1]
    };
    if (interactorData.SizeMode == vtkInternal::SIZE_MODE_RELATIVE)
    {
      // Only resize relative interactors if the screen size is valid
      if (this->Internal->ScreenSize[0] >= 0)
      {
        interactorRectangle.w *= this->Internal->ScreenSize[0];
        interactorRectangle.x *= this->Internal->ScreenSize[0];
      }
      if (this->Internal->ScreenSize[1] >= 0)
      {
        interactorRectangle.y *= this->Internal->ScreenSize[1];
        interactorRectangle.h *= this->Internal->ScreenSize[1];
      }
    }

    if (this->Internal->InteractionLib->AddOrUpdateInteractor(interactorData.ID, interactorRectangle, interactorData.Z)
      != IL::Result::Ok)
    {
      LOG_ERROR("Could not add or update interactor");
    }
  }

  if (this->Internal->InteractionLib->CommitInteractorUpdates()
    != IL::Result::Ok)
  {
    LOG_ERROR("Could not commit interactor updates");
  }


  // Subscribe to requested data
  this->GetToolByPortName("Gaze", this->Internal->GazeTool);
  if (this->Internal->GazeTool)
  {
    LOG_DEBUG("Gaze tool found, subscribing to gaze point data");
    this->Internal->InteractionLib->SubscribeGazePointData(&vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnGazePointDataCallback, this->Internal);
  }

  this->GetToolByPortName("LeftGazeOrigin", this->Internal->LeftGazeOriginTool);
  this->GetToolByPortName("RightGazeOrigin", this->Internal->RightGazeOriginTool);
  if (this->Internal->LeftGazeOriginTool || this->Internal->RightGazeOriginTool)
  {
    LOG_DEBUG("Gaze tool found, subscribing to gaze point data");
    this->Internal->InteractionLib->SubscribeGazeOriginData(&vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnGazeOriginDataCallback, this->Internal);
  }

  this->GetToolByPortName("Head", this->Internal->HeadTool);
  if (this->Internal->HeadTool)
  {
    LOG_DEBUG("Head tool found, subscribing to head pose data");
    this->Internal->InteractionLib->SubscribeHeadPoseData(&vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnHeadPoseDataCallback, this->Internal);
  }

  this->GetFieldDataSource("Interactor", this->Internal->InteractorFieldData);
  if (this->Internal->InteractorFieldData)
  {
    LOG_DEBUG("Interactor field data found, subscribing to gaze focus events");
    this->Internal->InteractionLib->SubscribeGazeFocusEvents(&vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnGazeFocusEventCallback, this->Internal);
  }

  this->GetFieldDataSource("Presence", this->Internal->PresenceFieldData);
  if (this->Internal->PresenceFieldData)
  {
    LOG_DEBUG("Interactor field data found, subscribing to gaze focus events");
    this->Internal->InteractionLib->SubscribePresenceData(&vtkPlusTobiiInteractionSDKTracker::vtkInternal::OnPresenceDataCallback, this->Internal);
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusTobiiInteractionSDKTracker::InternalDisconnect()
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: InternalDisconnect");
  return PLUS_SUCCESS;
};

//----------------------------------------------------------------------------
PlusStatus vtkPlusTobiiInteractionSDKTracker::InternalUpdate()
{
  LOG_DEBUG("vtkPlusTobiiInteractionSDKTracker: InternalUpdate");

  IL::Result result = this->Internal->InteractionLib->WaitAndUpdate();
  if (result < IL::Result::Ok)
  {
    LOG_ERROR("Error getting updating data from Tobii");
    return PLUS_FAIL;
  }
  else if (result == IL::Result::Warning_NoDeviceAvailable)
  {
    LOG_WARNING("No device connected. Ensure that your camera is plugged in, and Tobii experience is running");
    return PLUS_FAIL;
  }
  else if (result > IL::Result::Ok)
  {
    LOG_WARNING("Unknown warning");
    return PLUS_FAIL;
  }


  double unfilteredTimestamp = vtkIGSIOAccurateTimer::GetSystemTime();

  if (this->Internal->HeadTool)
  {
    this->ToolTimeStampedUpdate(
      this->Internal->HeadTool->GetId(),
      this->Internal->HeadToTracker,
      this->Internal->HeadPoseStatus,
      this->FrameNumber,
      unfilteredTimestamp);
  }

  if (this->Internal->GazeTool)
  {
    this->ToolTimeStampedUpdate(
      this->Internal->GazeTool->GetId(),
      this->Internal->GazeToTracker,
      this->Internal->GazePointDataStatus,
      this->FrameNumber,
      unfilteredTimestamp);
  }

  if (this->Internal->LeftGazeOriginTool)
  {
    this->ToolTimeStampedUpdate(
      this->Internal->LeftGazeOriginTool->GetId(),
      this->Internal->LeftGazeOriginToTracker,
      this->Internal->LeftGazeOriginStatus,
      this->FrameNumber,
      unfilteredTimestamp);
  }

  if (this->Internal->RightGazeOriginTool)
  {
    this->ToolTimeStampedUpdate(
      this->Internal->RightGazeOriginTool->GetId(),
      this->Internal->RightGazeOriginToTracker,
      this->Internal->RightGazeOriginStatus,
      this->FrameNumber,
      unfilteredTimestamp);
  }

  if (this->Internal->InteractorFieldData)
  {
    std::string interactorName = this->Internal->NoFocusName;
    auto interactorDataIt = this->Internal->Interactors.find(this->Internal->FocusedInteractor);
    if (this->Internal->HasFocus && interactorDataIt != this->Internal->Interactors.end())
    {
      interactorName = interactorDataIt->second.Name;
    }
    igsioFieldMapType fieldMap;
    fieldMap[this->Internal->InteractorFieldData->GetId()].first = FRAMEFIELD_NONE;
    fieldMap[this->Internal->InteractorFieldData->GetId()].second = interactorName;
    this->Internal->InteractorFieldData->AddItem(fieldMap, this->FrameNumber, unfilteredTimestamp);
  }

  if (this->Internal->PresenceFieldData)
  {
    std::string presenceString = "Unknown";
    if (this->Internal->Presence == IL::Presence::Away)
    {
      presenceString = "Away";
    }
    else if (this->Internal->Presence == IL::Presence::Present)
    {
      presenceString = "Present";
    }
    igsioFieldMapType fieldMap;
    fieldMap[this->Internal->PresenceFieldData->GetId()].first = FRAMEFIELD_NONE;
    fieldMap[this->Internal->PresenceFieldData->GetId()].second = presenceString;
    this->Internal->PresenceFieldData->AddItem(fieldMap, this->FrameNumber, unfilteredTimestamp);
  }

  ++this->FrameNumber;
  return PLUS_SUCCESS;
};
