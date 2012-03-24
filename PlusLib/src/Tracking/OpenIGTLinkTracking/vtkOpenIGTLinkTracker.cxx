/*=Plus=header=begin======================================================
  Program: Plus
  Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
  See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"

#include "vtkOpenIGTLinkTracker.h"
#include "vtkPlusIgtlMessageCommon.h"

#include <sstream>

#include "vtkMatrix4x4.h"
#include "vtkObjectFactory.h"
#include "vtksys/SystemTools.hxx"
#include "vtkTransform.h"
#include "vtkXMLDataElement.h"

#include "PlusConfigure.h"
#include "vtkTracker.h"
#include "vtkTrackerTool.h"
#include "vtkTrackerBuffer.h"

#include "igtlMessageHeader.h"
#include "igtlTransformMessage.h"
#include "igtlPositionMessage.h"
#include "igtlPlusClientInfoMessage.h"

vtkStandardNewMacro(vtkOpenIGTLinkTracker);

//----------------------------------------------------------------------------
vtkOpenIGTLinkTracker::vtkOpenIGTLinkTracker()
{
  this->ServerAddress = NULL; 
  this->ServerPort = -1; 
  this->ClientSocket = igtl::ClientSocket::New(); 
}

//----------------------------------------------------------------------------
vtkOpenIGTLinkTracker::~vtkOpenIGTLinkTracker() 
{
  if ( this->Tracking )
  {
    this->StopTracking();
  }
}

//----------------------------------------------------------------------------
void vtkOpenIGTLinkTracker::PrintSelf( ostream& os, vtkIndent indent )
{
  vtkTracker::PrintSelf( os, indent );
}

//----------------------------------------------------------------------------
std::string vtkOpenIGTLinkTracker::GetSdkVersion()
{
  std::ostringstream version; 
  version << "OpenIGTLink v" << PLUS_OPENIGTLINK_VERSION; 
  return version.str(); 
}

//----------------------------------------------------------------------------
PlusStatus vtkOpenIGTLinkTracker::Connect()
{
  LOG_TRACE( "vtkOpenIGTLinkTracker::Connect" ); 

  if ( this->ClientSocket->GetConnected() )
  {
    return PLUS_SUCCESS; 
  }

  if ( this->ServerAddress == NULL )
  {
    LOG_ERROR("Unable to connect OpenIGTLink server - server address is NULL" ); 
    return PLUS_FAIL; 
  }

  if ( this->ServerPort < 0 )
  {
    LOG_ERROR("Unable to connect OpenIGTLink server - server port is invalid: " << this->ServerPort ); 
    return PLUS_FAIL; 
  }

  int r = this->ClientSocket->ConnectToServer( this->ServerAddress, this->ServerPort );

  if ( r != 0 )
  {
    LOG_ERROR( "Cannot connect to the server (" << this->ServerAddress << ":" << this->ServerPort << ")." );
    return PLUS_FAIL;
  }
  else
  {
    LOG_TRACE( "Client successfully connected to server (" << this->ServerAddress << ":" << this->ServerPort << ")."  );
  }

  // Clear buffers on connect
  this->ClearAllBuffers(); 

  // Wait before we send thre clinet info request 
  vtkAccurateTimer::Delay(1.0); 

  // Send clinet info request to the server
  PlusIgtlClientInfo clientInfo; 
  // We need TRANSFORM message type
  clientInfo.IgtlMessageTypes.push_back("TRANSFORM"); 

  // We need the following tool names from the server 
  for ( ToolIteratorType it = this->GetToolIteratorBegin(); it != this->GetToolIteratorEnd(); ++it )
  {
    PlusTransformName tName( it->second->GetToolName(), this->GetToolReferenceFrameName() ); 
    clientInfo.TransformNames.push_back( tName ); 
  }

  // Pack client info message 
  igtl::PlusClientInfoMessage::Pointer clientInfoMsg = igtl::PlusClientInfoMessage::New(); 
  clientInfoMsg->SetClientInfo(clientInfo); 
  clientInfoMsg->Pack(); 

  // Send message to server 
  int retValue = 0, numOfTries = 0; 
  while ( retValue == 0 && numOfTries < 3 )
  {
    retValue = this->ClientSocket->Send( clientInfoMsg->GetPackPointer(), clientInfoMsg->GetPackSize() ); 
    numOfTries++; 
  }

  if ( retValue == 0 )
  {
    LOG_ERROR("Failed to send PlusClientInfo message to server!"); 
    return PLUS_FAIL; 
  }

  return PLUS_SUCCESS; 
}

//----------------------------------------------------------------------------
PlusStatus vtkOpenIGTLinkTracker::Disconnect()
{
  LOG_TRACE( "vtkOpenIGTLinkTracker::Disconnect" ); 
  this->ClientSocket->CloseSocket(); 
  return this->StopTracking(); 
}

//----------------------------------------------------------------------------
PlusStatus vtkOpenIGTLinkTracker::Probe()
{
  LOG_TRACE( "vtkOpenIGTLinkTracker::Probe" ); 

  if ( this->ClientSocket->GetConnected() )
  {
    return PLUS_SUCCESS; 
  }
  
  return PLUS_FAIL; 
} 

//----------------------------------------------------------------------------
PlusStatus vtkOpenIGTLinkTracker::InternalStartTracking()
{
  LOG_TRACE( "vtkOpenIGTLinkTracker::InternalStartTracking" ); 
  if ( this->Tracking )
  {
    return PLUS_SUCCESS;
  }

  return this->Connect(); 
}

//----------------------------------------------------------------------------
PlusStatus vtkOpenIGTLinkTracker::InternalStopTracking()
{
  LOG_TRACE( "vtkOpenIGTLinkTracker::InternalStopTracking" ); 
  
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkOpenIGTLinkTracker::InternalUpdate()
{
  LOG_TRACE( "vtkOpenIGTLinkTracker::InternalUpdate" ); 

  if ( ! this->Tracking )
  {
    LOG_ERROR("called Update() when not tracking" );
    return PLUS_FAIL;
  }

  igtl::MessageHeader::Pointer headerMsg;
  headerMsg = igtl::MessageHeader::New();
  headerMsg->InitPack();

  int retValue = 0, numOfTries = 0; 
  while ( retValue == 0 && numOfTries < 3 )
  {
    retValue = this->ClientSocket->Receive( headerMsg->GetPackPointer(), headerMsg->GetPackSize() );
    numOfTries++; 
  }

  // No message received - server disconnected 
  if ( retValue == 0 ) 
  {
    LOG_ERROR("OpenIGTLink tracker connection lost with server - tracking stopped!");
    this->Disconnect(); 
    return PLUS_FAIL; 
  }

  // Received data is not as we expected
  if ( retValue != headerMsg->GetPackSize() )
  {
    LOG_ERROR("Couldn't receive data from OpenIGTLink tracker"); 
    return PLUS_FAIL; 
  }

  vtkSmartPointer<vtkMatrix4x4> toolMatrix = vtkSmartPointer<vtkMatrix4x4>::New(); 
  double unfilteredTimestamp = 0; 

  igtl::TimeStamp::Pointer igtlTimestamp = igtl::TimeStamp::New(); 
  igtl::Matrix4x4 igtlMatrix;
  igtl::IdentityMatrix(igtlMatrix);
  std::string igtlTransformName; 
  
  headerMsg->Unpack();
  if (strcmp(headerMsg->GetDeviceType(), "TRANSFORM") == 0)
  {
    if ( vtkPlusIgtlMessageCommon::UnpackTransformMessage(headerMsg, this->ClientSocket.GetPointer(), toolMatrix, igtlTransformName, unfilteredTimestamp) != PLUS_SUCCESS )
    {
      LOG_ERROR("Couldn't receive transform message from server!"); 
      return PLUS_FAIL;
    }
  }
  else if (strcmp(headerMsg->GetDeviceType(), "POSITION") == 0)
  {
    float position[3] = {0}; 
    if ( vtkPlusIgtlMessageCommon::UnpackPositionMessage(headerMsg, this->ClientSocket.GetPointer(), position, igtlTransformName, unfilteredTimestamp) != PLUS_SUCCESS )
    {
      LOG_ERROR("Couldn't receive position message from server!"); 
      return PLUS_FAIL;
    }

    toolMatrix->Identity(); 
    toolMatrix->SetElement(0,3, position[0]); 
    toolMatrix->SetElement(1,3, position[1]); 
    toolMatrix->SetElement(2,3, position[2]); 
  }
  else
  {
    // if the data type is unknown, skip reading. 
    this->ClientSocket->Skip(headerMsg->GetBodySizeToRead(), 0);
    return PLUS_SUCCESS; 
  }

  // Set transform name
  PlusTransformName transformName;
  if ( transformName.SetTransformName( igtlTransformName.c_str() ) != PLUS_SUCCESS )
  {
    LOG_ERROR("Failed to update tracker tool - unrecognized transform name: " << igtlTransformName ); 
    return PLUS_FAIL; 
  }

  if ( this->ToolTimeStampedUpdate(transformName.From().c_str(), toolMatrix, TOOL_OK, unfilteredTimestamp) != PLUS_SUCCESS )
  {
    LOG_ERROR("ToolTimeStampedUpdate failed for tool: " << transformName.From() << " with timestamp: " << std::fixed << unfilteredTimestamp); 
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkOpenIGTLinkTracker::ReadConfiguration( vtkXMLDataElement* config )
{
  // Read superclass configuration first
  Superclass::ReadConfiguration( config ); 

  LOG_TRACE( "vtkOpenIGTLinkTracker::ReadConfiguration" ); 
  if ( config == NULL ) 
  {
    LOG_ERROR("Unable to find OpenIGTLinkTracker XML data element");
    return PLUS_FAIL; 
  }

	vtkXMLDataElement* dataCollectionConfig = config->FindNestedElementWithName("DataCollection");
	if (dataCollectionConfig == NULL)
  {
    LOG_ERROR("Cannot find DataCollection element in XML tree!");
		return PLUS_FAIL;
	}

  vtkXMLDataElement* trackerConfig = dataCollectionConfig->FindNestedElementWithName("Tracker"); 
  if (trackerConfig == NULL) 
  {
    LOG_ERROR("Cannot find Tracker element in XML tree!");
		return PLUS_FAIL;
  }

  const char* serverAddress = trackerConfig->GetAttribute("ServerAddress"); 
  if ( serverAddress != NULL )
  {
    this->SetServerAddress(serverAddress); 
  }
  else
  {
    LOG_ERROR("Unable to find ServerAddress attribute!"); 
    return PLUS_FAIL; 
  }

  int serverPort = -1; 
  if ( trackerConfig->GetScalarAttribute("ServerPort", serverPort ) )
  {
    this->SetServerPort(serverPort); 
  }
  else
  {
    LOG_ERROR("Unable to find ServerPort attribute!"); 
    return PLUS_FAIL; 
  } 


  return PLUS_SUCCESS;
}
