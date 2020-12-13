/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "PlusConfigure.h"
#include "vtkPlusIntelRealSense.h"

// Local includes
#include "vtkPlusChannel.h"
#include "vtkPlusDataSource.h"

// IntelRealSense includes
#include <rs.hpp>

// stl includes
#include <sstream>
#include <vector>

// VTK includes
#include <vtkImageData.h>
#include <vtkObjectFactory.h>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPlusIntelRealSense);

//----------------------------------------------------------------------------
// constants
static const unsigned int REALSENSE_DEFAULT_FRAME_WIDTH = 640;
static const unsigned int REALSENSE_DEFAULT_FRAME_HEIGHT = 480;
static const unsigned int REALSENSE_DEFAULT_FRAME_RATE = 30;
static const bool DEFAULT_ALIGN_DEPTH_STREAM = false;
static const bool DEFAULT_USE_REALSENSE_COLORIZER = false;

//----------------------------------------------------------------------------
class vtkPlusIntelRealSense::vtkInternal
{
public:
  vtkPlusIntelRealSense * External;

  vtkInternal(vtkPlusIntelRealSense* external)
    : External(external)
  {
  }

  virtual ~vtkInternal()
  {
  }

  struct Stream
  {
    Stream(
      std::string serial_num,
      rs2_stream stream_type,
      unsigned int width,
      unsigned int height,
      unsigned int frame_rate,
      std::string plus_source_id,
      bool align_depth_stream,
      bool use_realsense_colorizer)
    {
      this->DeviceSerialNumber = serial_num;
      this->StreamType = stream_type;
      this->Width = width;
      this->Height = height;
      this->FrameRate = frame_rate;
      this->PlusSourceId = plus_source_id;
      this->AlignDepthStream = align_depth_stream;
      this->UseRealSenseColorizer = use_realsense_colorizer;

      this->Align = nullptr;
    }

    // configuration member vars
    std::string DeviceSerialNumber;
    rs2_stream StreamType;
    rs2_format StreamFormat;
    unsigned int Width;
    unsigned int Height;
    unsigned int FrameRate;

    // PLUS member vars
    std::string PlusSourceId;
    vtkPlusDataSource* PlusDataSource; // set when frame created for the first time

    // settings for processing the depth frames
    bool UseRealSenseColorizer;
    bool AlignDepthStream;
    float DepthScaleToMm;

    // RealSense member vars
    rs2::device Device;
    rs2::sensor Sensor;
    rs2::stream_profile StreamProfile;
    rs2::align* Align;  // rs2::align doesn't have a default constructor, so we must use a pointer
  };

  // MEMBER FUNCTIONS

  // Sets the stream format from rs2_stream type
  // Returns PLUS_FAIL if PLUS doesn't currently support this stream type
  PlusStatus SetStreamFormat(rs2_stream stream_type, rs2_format& stream_format);

  // Get requested device by serial number
  // If provided serial number is an empty string, device is set to the first available RS device
  // If no devices are connected (no serial num specified), or the device with the provided serial
  // number is not connected PLUS_FAIL is returned
  PlusStatus GetRequestedRSDevice(std::string serial_num, rs2::device& device);

  // Get requested sensor by stream type
  // Returns PLUS_FAIL if sensor type not available for the provided device
  PlusStatus GetRequestedRSSensor(const rs2::device& device, rs2_stream stream_type, rs2::sensor& requested_sensor);

  // Get the requested stream_profile to generate the data stream desired by the user
  // If desired stream is not available this logs an error message
  // and returns PLUS_FAIL
  PlusStatus GetRequestedRSStreamProfile(
    const rs2::sensor& sensor,
    rs2_stream stream_type,
    rs2_format format,
    unsigned int width,
    unsigned int height,
    unsigned int frame_rate,
    rs2::stream_profile& requested_stream_profile
  );

  // Configure PLUS channel
  PlusStatus ConfigurePLUSChannel(const Stream& stream, vtkPlusDataSource* source);

  // Get the depth scale of a selected device
  PlusStatus SetDepthScaleToMm(rs2::sensor& sensor, float& depth_scale_to_mm);

  // Find the color channel to align the depth frame to
  PlusStatus SetStreamToAlignTo(const Stream& stream, rs2::align* align);

  // Logs info about the connected RealSense devices
  void PrintRSDevicesInfo();

  // Logs info about the selected streams
  void PrintStreamList();

  // Callback for new frames
  void FrameCallback(const vtkInternal::Stream& stream, rs2::frame frame);

  // MEMBER VARIABLES
   
  // List of stream parameters and their related streaming profiles
  std::vector<Stream> StreamList;

  rs2::context RSContext;
};

//----------------------------------------------------------------------------
// Implement methods from vtkPlusIntelRealSense::vtkInternal
//----------------------------------------------------------------------------

PlusStatus vtkPlusIntelRealSense::vtkInternal::SetStreamFormat(rs2_stream stream_type, rs2_format& stream_format)
{
  switch (stream_type)
  {
  case RS2_STREAM_COLOR:
    stream_format = RS2_FORMAT_RGB8;
    return PLUS_SUCCESS;
  case RS2_STREAM_DEPTH:
    stream_format = RS2_FORMAT_Z16;
    return PLUS_SUCCESS;
  }

  LOG_ERROR("Unsupported stream type provided to SetStreamFormat.");
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::vtkInternal::GetRequestedRSDevice(std::string requested_serial_num, rs2::device& requested_device)
{
  rs2::device_list devices = this->RSContext.query_devices();

  // if no RealSense devices are found, log error and fail
  if (devices.size() == 0)
  {
    LOG_ERROR("No RealSense devices connected. Please connect a RealSense device and try again.");
    return PLUS_FAIL;
  }

  // if empty string provided for request_serial_num, return first connected RealSense device
  if (requested_serial_num.length() == 0)
  {
    requested_device = devices[0];
    return PLUS_SUCCESS;
  }

  // devices connected and serial number provided, try to find requested device
  for (rs2::device device : devices)
  {
    if (!device.supports(RS2_CAMERA_INFO_SERIAL_NUMBER))
    {
      // for some reason this device doesn't support serial number, continue
      continue;
    }

    std::string device_serial_num = device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
    if (igsioCommon::IsEqualInsensitive(requested_serial_num, device_serial_num))
    {
      requested_device = device;
      return PLUS_SUCCESS;
    }
  }
  
  // requested device not found, return fail
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::vtkInternal::GetRequestedRSSensor(const rs2::device& device, rs2_stream stream_type, rs2::sensor& requested_sensor)
{
  std::vector<rs2::sensor> sensors = device.query_sensors();

  for (rs2::sensor sensor : sensors)
  {
    if (stream_type == RS2_STREAM_COLOR && sensor.is<rs2::color_sensor>())
    {
      requested_sensor = sensor;
      return PLUS_SUCCESS;
    }
    else if (stream_type == RS2_STREAM_DEPTH && sensor.is<rs2::depth_sensor>())
    {
      requested_sensor = sensor;
      return PLUS_SUCCESS;
    }
  }

  // sensor producing requested stream type not found, return fail
  LOG_ERROR("No sensor was found for the specified device which produced the correct stream type.")
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::vtkInternal::GetRequestedRSStreamProfile(
  const rs2::sensor& sensor,
  rs2_stream stream_type,
  rs2_format format,
  unsigned int width,
  unsigned int height,

  unsigned int frame_rate,
  rs2::stream_profile& requested_stream_profile)
{
  std::vector<rs2::stream_profile> stream_profiles = sensor.get_stream_profiles();

  for (rs2::stream_profile stream_profile : stream_profiles)
  {
    bool correct_stream_type = (stream_type == stream_profile.stream_type());
    bool correct_format = (format == stream_profile.format());

    // check if format can be treated as a video stream
    if (!stream_profile.is<rs2::video_stream_profile>())
    {
      // this stream is not a video stream
      continue;
    }

    rs2::video_stream_profile vsp = stream_profile.as<rs2::video_stream_profile>();

    bool correct_width = (width == vsp.width());
    bool correct_height = (height == vsp.height());
    bool correct_frame_rate = (frame_rate == vsp.fps());

    // if correct stream format, set requested_stream_profile and return success
    if (correct_stream_type && correct_format && correct_width && correct_height && correct_frame_rate)
    {
      requested_stream_profile = stream_profile;
      return PLUS_SUCCESS;
    }
  }

  // requested format not found
  LOG_ERROR("The selected sensor cannot produce the requested format. Please see the datasheet for your RealSense device to ensure appropriate parameters have been selected.");
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::vtkInternal::ConfigurePLUSChannel(const Stream& stream, vtkPlusDataSource* source)
{
  if (stream.StreamType == RS2_STREAM_COLOR)
  {
    source->SetImageType(US_IMG_RGB_COLOR);
    source->SetPixelType(VTK_UNSIGNED_CHAR);
    source->SetNumberOfScalarComponents(3);
    source->SetInputFrameSize(stream.Width, stream.Height, 1);
    return PLUS_SUCCESS;
  }
  else if (stream.StreamType == RS2_STREAM_DEPTH && stream.UseRealSenseColorizer)
  {
    // depth output is RGB from rs2::colorizer
    source->SetImageType(US_IMG_RGB_COLOR);
    source->SetPixelType(VTK_UNSIGNED_CHAR);
    source->SetNumberOfScalarComponents(3);
    source->SetInputFrameSize(stream.Width, stream.Height, 1);
    return PLUS_SUCCESS;
  }
  else if (stream.StreamType == RS2_STREAM_DEPTH && !stream.UseRealSenseColorizer)
  {
    // depth output is raw depth data
    source->SetImageType(US_IMG_BRIGHTNESS);
    source->SetPixelType(VTK_TYPE_UINT16);
    source->SetNumberOfScalarComponents(1);
    source->SetInputFrameSize(stream.Width, stream.Height, 1);
    return PLUS_SUCCESS;
  }

  LOG_ERROR("Unexpected stream configuration provided to ConfigurePLUSChannel.");
  return PLUS_FAIL;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::vtkInternal::SetDepthScaleToMm(rs2::sensor& sensor, float& depth_scale_to_mm)
{
  // check if this sensor is indeed a depth_sensor
  if (!sensor.is<rs2::depth_sensor>())
  {
    LOG_ERROR("Sensor provided as parameter to SetDepthScaleToMm is not a depth sensor. Failed to get DepthScaleToMm.");
    return PLUS_FAIL;
  }

  rs2::depth_sensor depth_sensor = sensor.as<rs2::depth_sensor>();
  depth_scale_to_mm = depth_sensor.get_depth_scale();
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::vtkInternal::SetStreamToAlignTo(const Stream& stream, rs2::align* align)
{
  // TODO: implement
  //Given a vector of streams, we try to find a depth stream and another stream to align depth with.
  //We prioritize color streams to make the view look better.
  //If color is not available, we take another stream that (other than depth)
  //rs2_stream align_to = RS2_STREAM_ANY;
  //bool depth_stream_found = false;
  //bool color_stream_found = false;
  //for (rs2::stream_profile sp : streams)
  //{
  //  rs2_stream profile_stream = sp.stream_type();
  //  if (profile_stream != RS2_STREAM_DEPTH)
  //  {
  //    if (!color_stream_found)         //Prefer color
  //      align_to = profile_stream;

  //    if (profile_stream == RS2_STREAM_COLOR)
  //    {
  //      color_stream_found = true;
  //    }
  //  }
  //  else
  //  {
  //    depth_stream_found = true;
  //  }
  //}

  //if (!depth_stream_found)
  //{
  //  LOG_ERROR("Failed to align RealSense streams. No Intel RealSense Depth stream available. Please proivde a depth stream, or set AlignDepthStream to FALSE.");
  //  return PLUS_FAIL;
  //}

  //if (align_to == RS2_STREAM_ANY)
  //{
  //  LOG_ERROR("Failed to align RealSense streams. No Intel RealSense stream found to align with Depth. Please proivde an RGB stream, or set AlignDepthStream to FALSE");
  //  return PLUS_FAIL;
  //}
  //this->AlignTo = align_to;
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
void vtkPlusIntelRealSense::vtkInternal::PrintRSDevicesInfo()
{
  LOG_INFO("IMPLMENT PRINT RS DEVICES INFO!");
}

//----------------------------------------------------------------------------
void vtkPlusIntelRealSense::vtkInternal::PrintStreamList()
{
  LOG_INFO(std::endl << "Listing enabled RealSense streams...");

  for (Stream& stream : StreamList)
  {
    // stream type
    std::stringstream stream_str;
    stream_str << "---------------" << std::endl;
    if (stream.StreamType == RS2_STREAM_COLOR)
    {
      stream_str << "Color stream";
    }
    else if (stream.StreamType == RS2_STREAM_DEPTH)
    {
      stream_str << "Depth stream";
    }

    // device info
    if (stream.Device.supports(RS2_CAMERA_INFO_NAME))
    {
      stream_str << " on " << stream.Device.get_info(RS2_CAMERA_INFO_NAME);
    }
    else
    {
      stream_str << " on unknown RealSense device";
    }

    // device serial number
    if (stream.Device.supports(RS2_CAMERA_INFO_SERIAL_NUMBER))
    {
      stream_str << " (SN: " << stream.Device.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER) << ")";
    }
    else
    {
      stream_str << " (unknown SN)";
    }
    
    stream_str << std::endl;

    // PlusSourceId
    stream_str << "Plus source id: " << stream.PlusSourceId << std::endl;

    // frame size
    stream_str << "Frame size: [" << stream.Width << ", " << stream.Height << "]" << std::endl;

    // frame rate
    stream_str << "Frame rate: " << stream.FrameRate << std::endl;

    // depth stream only options
    if (stream.StreamType == RS2_STREAM_DEPTH)
    {
      // UseRealSenseColorizer
      stream_str << "Use RealSense colorizer: " << (stream.UseRealSenseColorizer ? "TRUE" : "FALSE") << std::endl;

      // AlignDepthStreamToColorStream
      stream_str << "Align depth stream to color stream: " << (stream.AlignDepthStream ? "TRUE" : "FALSE") << std::endl;

      // TODO: Print Id of stream aligning to if align enabled
    }

    LOG_INFO(stream_str.str());
  }
}

//----------------------------------------------------------------------------
void vtkPlusIntelRealSense::vtkInternal::FrameCallback(const vtkInternal::Stream& stream, rs2::frame frame)
{
  vtkPlusDataSource* src = stream.PlusDataSource;
  src->AddItem(
    (void*)frame.get_data(),
    src->GetInputImageOrientation(),
    src->GetInputFrameSize(),
    src->GetPixelType(),
    src->GetNumberOfScalarComponents(),
    src->GetImageType(),
    0, // number of bytes to skip
    frame.get_frame_number()
  );
}

//----------------------------------------------------------------------------
// Implement methods from vtkPlusIntelRealSense
//----------------------------------------------------------------------------

//----------------------------------------------------------------------------
vtkPlusIntelRealSense::vtkPlusIntelRealSense()
  : Internal(new vtkInternal(this))
{
  this->RequireImageOrientationInConfiguration = true;
  this->StartThreadForInternalUpdates = true;
  this->InternalUpdateRate = REALSENSE_DEFAULT_FRAME_RATE;
  this->AcquisitionRate = REALSENSE_DEFAULT_FRAME_RATE;
}

//----------------------------------------------------------------------------
vtkPlusIntelRealSense::~vtkPlusIntelRealSense()
{
  delete Internal;
  Internal = nullptr;
}

//----------------------------------------------------------------------------
void vtkPlusIntelRealSense::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  // TODO: Re-implement this without hard-coding
  //os << indent << "Intel RealSense 3d Camera: D415" << std::endl;
  //os << indent << "RgbDataSourceName: " << RgbDataSourceName << std::endl;
  //os << indent << "DepthDataSourceName: " << DepthDataSourceName << std::endl;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::ReadConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_READING(deviceConfig, rootConfigElement);
  
  // handle specification of AlignDepthStream at device level
  bool DeviceAlignDepthStreamSpecified = false;
  bool DeviceAlignDepthStreamValue = false;
  if (deviceConfig->GetAttribute("AlignDepthStream") != 0)
  {
    DeviceAlignDepthStreamSpecified = true;
    XML_READ_BOOL_ATTRIBUTE_NONMEMBER_OPTIONAL(AlignDepthStream, DeviceAlignDepthStreamValue, deviceConfig);
    LOG_WARNING("The AlignDepthStream attribute has been deprecated at the PLUS config Device level. We recommend specifying AlignDepthStream for each DEPTH DataSource individually.");
  }

  // handle specification of UseRealSenseColorizer at device level
  bool DeviceUseRSColorizerSpecified = false;
  bool DeviceUseRSColorizerValue = false;
  if (deviceConfig->GetAttribute("UseRealSenseColorizer") != 0)
  {
    DeviceUseRSColorizerSpecified = true;
    XML_READ_BOOL_ATTRIBUTE_NONMEMBER_OPTIONAL(UseRealSenseColorizer, DeviceUseRSColorizerValue, deviceConfig);
    LOG_WARNING("The UseRealSenseColorizer attribute has been deprecated at the PLUS config Device level. We recommend specifying UseRealSenseColorizer for each DEPTH DataSource individually.");
  }

  // read DataSources
  XML_FIND_NESTED_ELEMENT_REQUIRED(dataSourcesElement, deviceConfig, "DataSources");
  for (int nestedElementIndex = 0; nestedElementIndex < dataSourcesElement->GetNumberOfNestedElements(); nestedElementIndex++)
  {
    vtkXMLDataElement* dataElement = dataSourcesElement->GetNestedElement(nestedElementIndex);
    if (STRCASECMP(dataElement->GetName(), "DataSource") != 0)
    {
      // if this is not a data source element, skip it
      continue;
    }

    if (dataElement->GetAttribute("Type") != NULL && STRCASECMP(dataElement->GetAttribute("Type"), "Video") == 0)
    {
      // this is a video element
      // get tool ID
      const char* toolId = dataElement->GetAttribute("Id");
      if (toolId == NULL)
      {
        // tool doesn't have ID needed to generate transform
        LOG_ERROR("Failed to initialize IntelRealSense DataSource: Id is missing");
        continue;
      }

      // get Intel RealSense video parameters for this source

      // get optional RealSense device serial number (for multi-camera setups)
      std::string devSerialNum = ""; // if null string will use first available device
      XML_READ_STRING_ATTRIBUTE_NONMEMBER_OPTIONAL(DeviceSerialNumber, devSerialNum, dataElement);

      // source type
      rs2_stream streamType;
      XML_READ_ENUM2_ATTRIBUTE_NONMEMBER_REQUIRED(FrameType, streamType, dataElement, "RGB", RS2_STREAM_COLOR, "DEPTH", RS2_STREAM_DEPTH);
      
      // frame size & frame rate
      int frameSize[2] = { REALSENSE_DEFAULT_FRAME_WIDTH, REALSENSE_DEFAULT_FRAME_HEIGHT };
      XML_READ_VECTOR_ATTRIBUTE_NONMEMBER_OPTIONAL(int, 2, FrameSize, frameSize, dataElement);
      int frameRate = REALSENSE_DEFAULT_FRAME_RATE;
      XML_READ_SCALAR_ATTRIBUTE_NONMEMBER_OPTIONAL(int, FrameRate, frameRate, dataElement);

      // align depth stream
      bool alignDepthStream = DEFAULT_ALIGN_DEPTH_STREAM;
      if (streamType != RS2_STREAM_DEPTH && dataElement->GetAttribute("AlignDepthStream") != 0)
      {
        // applies to depth streams only
        LOG_ERROR("AlignDepthStream DataSource attribute only applies to DataSources with FrameType=DEPTH. Please remove the AlignDepthStream attribute from DataSource " << toolId);
        return PLUS_FAIL;
      }
      else if (DeviceAlignDepthStreamSpecified && dataElement->GetAttribute("AlignDepthStream") != 0)
      {
        // should only be able to specify AlignDepthStream at Device level (legacy) OR DataSource level
        LOG_ERROR("Attempt to specify AlignDepthStream attribute in both the Device and DataSource XML blocks. Please remove the AlignDepthStream attribute from the RealSense Device block.");
        return PLUS_FAIL;
      }
      else if (DeviceAlignDepthStreamSpecified)
      {
        // use the device level AlignDepthStream value
        alignDepthStream = DeviceAlignDepthStreamValue;
      }
      else
      {
        // read AlignDepthStream value from DataSource
        XML_READ_BOOL_ATTRIBUTE_NONMEMBER_OPTIONAL(AlignDepthStream, alignDepthStream, dataElement);
      }

      // use RealSense colorizer
      bool useRealSenseColorizer = DEFAULT_USE_REALSENSE_COLORIZER;
      if (streamType != RS2_STREAM_DEPTH && dataElement->GetAttribute("UseRealSenseColorizer") != 0)
      {
        // applies to depth streams only
        LOG_ERROR("UseRealSenseColorizer DataSource attribute only applies to DataSources with FrameType=DEPTH. Please remove the UseRealSenseColorizer attribute from DataSource " << toolId);
        return PLUS_FAIL;
      }
      else if (DeviceUseRSColorizerSpecified && dataElement->GetAttribute("UseRealSenseColorizer") != 0)
      {
        // should only be able to specify UseRealSenseColorizer at Device level (legacy) OR DataSource level
        LOG_ERROR("Attempt to specify UseRealSenseColorizer attribute in both the Device and DataSource XML blocks. Please remove the UseRealSenseColorizer attribute from the RealSense Device block.");
        return PLUS_FAIL;
      }
      else if (DeviceUseRSColorizerSpecified)
      {
        // use the device level UseRealSenseColorizer value
        useRealSenseColorizer = DeviceUseRSColorizerValue;
      }
      else
      {
        // read UseRealSenseColorizer value from DataSource
        XML_READ_BOOL_ATTRIBUTE_NONMEMBER_OPTIONAL(UseRealSenseColorizer, useRealSenseColorizer, dataElement);
      }

      // add stream to VideoSources
      vtkInternal::Stream stream(
        devSerialNum,
        streamType,
        frameSize[0],
        frameSize[1],
        frameRate,
        toolId,
        alignDepthStream,
        useRealSenseColorizer
      );
      this->Internal->StreamList.push_back(stream);
    }
    else
    {
      LOG_ERROR("DataSource with unknown Type.");
      return PLUS_FAIL;
    }
  }
  return PLUS_SUCCESS;
}

//-----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::WriteConfiguration(vtkXMLDataElement* rootConfigElement)
{
  XML_FIND_DEVICE_ELEMENT_REQUIRED_FOR_WRITING(deviceConfig, rootConfigElement);
  
  // TODO: implement writing of relevant params

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::InternalConnect()
{
  // loop over requested streams and setup their stream_profile's
  for (vtkInternal::Stream& stream : this->Internal->StreamList)
  {
    PlusStatus status;

    // get Plus data source for this stream
    status = this->GetVideoSource(stream.PlusSourceId.c_str(), stream.PlusDataSource);
    if (status != PLUS_SUCCESS)
    {
      LOG_ERROR("Failed to find video source for data source with Id: " << stream.PlusSourceId);
      return PLUS_FAIL;
    }

    // set the stream format
    status = this->Internal->SetStreamFormat(stream.StreamType, stream.StreamFormat);
    if (status != PLUS_SUCCESS)
    {
      // descriptive error already logged in SetStreamFormat
      return PLUS_FAIL;
    }

    // get the device to use for this stream
    status = this->Internal->GetRequestedRSDevice(stream.DeviceSerialNumber, stream.Device);
    if (status != PLUS_SUCCESS)
    {
      // descriptive error already logged in GetRequestedRSDevice
      return PLUS_FAIL;
    }

    // get the sensor to use for this stream
    status = this->Internal->GetRequestedRSSensor(stream.Device, stream.StreamType, stream.Sensor);
    if (status != PLUS_SUCCESS)
    {
      // descriptive error already logged in GetRequestedRSSensor
      return PLUS_FAIL;
    }

    // get the stream_profile required to retrieve the data format requested by the user
    status = this->Internal->GetRequestedRSStreamProfile(
      stream.Sensor,
      stream.StreamType,
      stream.StreamFormat,
      stream.Width,
      stream.Height,
      stream.FrameRate,
      stream.StreamProfile
    );
    if (status != PLUS_SUCCESS)
    {
      // descriptive error already logged in GetRequestedRSStreamProfile
      return PLUS_FAIL;
    }

    // set the depth scale for stream, if applicable
    if (stream.StreamType == RS2_STREAM_DEPTH)
    {
      status = this->Internal->SetDepthScaleToMm(stream.Sensor, stream.DepthScaleToMm);
      if (status != PLUS_SUCCESS)
      {
        // descriptive error already logged in SetDepthScaleToMm
        return PLUS_FAIL;
      }
    }

    // configure the PLUS channel for this stream
    status = this->Internal->ConfigurePLUSChannel(stream, stream.PlusDataSource);
    if (status != PLUS_SUCCESS)
    {
      // descriptive error already logged in ConfigurePLUSChannel
      return PLUS_FAIL;
    }
  }

  // now that all requested streams have been processed, we can
  // configure the align object for depth streams where alignment was requested
  for (vtkInternal::Stream& stream : this->Internal->StreamList)
  {
    if (stream.StreamType == RS2_STREAM_DEPTH && stream.AlignDepthStream)
    {
      if (this->Internal->SetStreamToAlignTo(stream, stream.Align) != PLUS_SUCCESS)
      {
        // descriptive error already logged in SetStreamToAlignTo
        return PLUS_FAIL;
      }
    }
  }

  // open the stream profiles
  for (vtkInternal::Stream& stream : this->Internal->StreamList)
  {
    stream.Sensor.open(stream.StreamProfile);
  }

  // log info about connected cameras for debugging
  this->Internal->PrintRSDevicesInfo();

  // log info about setup streams for debugging
  this->Internal->PrintStreamList();

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::InternalDisconnect()
{
  for (vtkInternal::Stream& stream : this->Internal->StreamList)
  {
    // close the stream
    stream.Sensor.close();

    // delete the stream's Align class
    if (stream.Align != nullptr)
    {
      delete stream.Align;
      stream.Align = nullptr;
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::InternalStartRecording()
{
  // loop over all requested streams and start
  for (vtkInternal::Stream& stream : this->Internal->StreamList)
  {
    stream.Sensor.start([&](rs2::frame f) {
      this->Internal->FrameCallback(stream, f);
      });
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::InternalStopRecording()
{
  // stop all streams in StreamList
  for (vtkInternal::Stream& stream : this->Internal->StreamList)
  {
    stream.Sensor.stop();
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::NotifyConfigured()
{
  // TODO: Implement some configuration checks here
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusIntelRealSense::InternalUpdate()
{
  this->FrameNumber++; 
  return PLUS_SUCCESS;
}