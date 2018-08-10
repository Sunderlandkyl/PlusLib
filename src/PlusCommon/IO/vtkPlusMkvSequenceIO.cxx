/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "vtkPlusMkvSequenceIO.h"

// PlusLib includes
#include "PlusTrackedFrame.h"
#include "vtkPlusTrackedFrameList.h"

#ifdef PLUS_USE_OpenIGTLink
#include "igtlCommon.h"
#ifdef OpenIGTLink_ENABLE_VIDEOSTREAMING
#include "vtkGenericCodecFactory.h"
#include "vtkPlusOpenIGTLinkCodec.h"
#ifdef OpenIGTLink_USE_VP9
#include <igtlVP9Encoder.h>
#include <igtlVP9Decoder.h>
#endif //OpenIGTLink_USE_VP9
#endif //OpenIGTLink_ENABLE_VIDEOSTREAMING
#endif //PLUS_USE_OpenIGTLink

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkPlusMkvSequenceIO);

//----------------------------------------------------------------------------
vtkPlusMkvSequenceIO::vtkPlusMkvSequenceIO()
  : vtkPlusSequenceIOBase()
  , MKVWriter(vtkSmartPointer<vtkMKVWriter>::New())
  , MKVReader(vtkSmartPointer<vtkMKVReader>::New())
  , Initialized(false)
  , InitialTimestamp(-1)
{
#ifdef OpenIGTLink_ENABLE_VIDEOSTREAMING
  vtkGenericCodecFactory* genericCodecFactory = vtkGenericCodecFactory::GetInstance();
#ifdef OpenIGTLink_USE_VP9
  vtkSmartPointer<vtkPlusOpenIGTLinkCodec> vp9Codec = vtkSmartPointer<vtkPlusOpenIGTLinkCodec>::New();
  vp9Codec->SetCodecFourCC(IGTL_VIDEO_CODEC_NAME_VP9);
  vp9Codec->Encoder = igtl::VP9Encoder::New();
  vp9Codec->Decoder = igtl::VP9Decoder::New();
  genericCodecFactory->RegisterCodec(vp9Codec);
#endif
#endif
}

//----------------------------------------------------------------------------
vtkPlusMkvSequenceIO::~vtkPlusMkvSequenceIO()
{
  this->Close();
}

//----------------------------------------------------------------------------
void vtkPlusMkvSequenceIO::PrintSelf(ostream& os, vtkIndent indent)
{
  Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::ReadImageHeader()
{
  this->MKVReader->SetFilename(this->FileName);
  if (!this->MKVReader->ReadHeader())
  {
    LOG_ERROR("Could not read mkv header!")
    return PLUS_FAIL;
  }
  
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
// Read the spacing and dimensions of the image.
PlusStatus vtkPlusMkvSequenceIO::ReadImagePixels()
{
  if (!this->MKVReader->ReadContents())
  {
    LOG_ERROR("Could not read mkv contents!");
    return PLUS_FAIL;
  }

  int totalFrameNumber = 0;
  vtkMKVReader::VideoTrackMap videoTracks = this->MKVReader->GetVideoTracks();
  for (vtkMKVReader::VideoTrackMap::iterator videoTrackIt = videoTracks.begin(); videoTrackIt != videoTracks.end(); ++videoTrackIt)
  {
    FrameSizeType frameSize = { videoTrackIt->second.Width, videoTrackIt->second.Height, 1 };
    int frameNumber = 0;
    for (vtkMKVReader::FrameInfoList::iterator frameIt = videoTrackIt->second.Frames.begin(); frameIt != videoTrackIt->second.Frames.end(); ++frameIt)
    {
      this->CreateTrackedFrameIfNonExisting(totalFrameNumber);
      PlusTrackedFrame* trackedFrame = this->TrackedFrameList->GetTrackedFrame(totalFrameNumber);
      trackedFrame->GetImageData()->SetImageOrientation(US_IMG_ORIENT_MF); // TODO: save orientation and type
      trackedFrame->GetImageData()->SetImageType(US_IMG_RGB_COLOR);
      trackedFrame->GetImageData()->AllocateFrame(frameSize, VTK_UNSIGNED_CHAR, 3);
      trackedFrame->SetTimestamp(frameIt->TimestampSeconds);

      double timestamp = frameIt->TimestampSeconds;
      if (!this->MKVReader->GetDecodedVideoFrame(videoTrackIt->first, frameNumber, trackedFrame->GetImageData()->GetImage(), timestamp))
      {
        LOG_ERROR("Could not decode frame!");
        continue;
      }

      ++frameNumber;
      ++totalFrameNumber;
    }
  }

  vtkMKVReader::MetadataTrackMap metadataTracks = this->MKVReader->GetMetadataTracks();
  for (vtkMKVReader::MetadataTrackMap::iterator metadataTrackIt = metadataTracks.begin(); metadataTrackIt != metadataTracks.end(); ++metadataTrackIt)
  {
    for (vtkMKVReader::FrameInfoList::iterator frameIt = metadataTrackIt->second.Frames.begin(); frameIt != metadataTrackIt->second.Frames.end(); ++frameIt)
    {
      for (unsigned int i = 0; i < this->TrackedFrameList->GetNumberOfTrackedFrames(); ++i)
      {
        PlusTrackedFrame* trackedFrame = this->TrackedFrameList->GetTrackedFrame(i);
        if (trackedFrame->GetTimestamp() != frameIt->TimestampSeconds)
        {
          continue;
        }
        std::string frameField = (char*)frameIt->Data->GetPointer(0);
        trackedFrame->SetCustomFrameField(metadataTrackIt->second.Name, frameField);
      }
    }
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::PrepareImageFile()
{
  this->WriteInitialImageHeader();
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
bool vtkPlusMkvSequenceIO::CanReadFile(const std::string& filename)
{
  if (PlusCommon::IsEqualInsensitive(vtksys::SystemTools::GetFilenameLastExtension(filename), ".mkv"))
  {
    return true;
  }

  if (PlusCommon::IsEqualInsensitive(vtksys::SystemTools::GetFilenameLastExtension(filename), ".webm"))
  {
    return true;
  }

  return false;
}

//----------------------------------------------------------------------------
bool vtkPlusMkvSequenceIO::CanWriteFile(const std::string& filename)
{
  if (PlusCommon::IsEqualInsensitive(vtksys::SystemTools::GetFilenameLastExtension(filename), ".mkv"))
  {
    return true;
  }

  return false;
}

//----------------------------------------------------------------------------
/** Assumes SetFileName has been called with a valid file name. */
PlusStatus vtkPlusMkvSequenceIO::WriteInitialImageHeader()
{
  this->MKVWriter->SetFilename(this->FileName);

  //TODO: Allow users to specify FourCC code
  this->EncodingType = IGTL_VIDEO_CODEC_NAME_VP9;

  if (this->TrackedFrameList->Size() == 0)
  {
    LOG_ERROR("Could not write MKV header, no tracked frames")
    return PLUS_FAIL;
  }

  this->MKVWriter->WriteHeader();

  int* dimensions = this->TrackedFrameList->GetTrackedFrame(0)->GetImageData()->GetImage()->GetDimensions();
  this->Dimensions[0] = dimensions[0];
  this->Dimensions[1] = dimensions[1];
  this->Dimensions[2] = dimensions[2];
  this->VideoTrackNumber = this->MKVWriter->AddVideoTrack("Video", this->EncodingType, dimensions[0], dimensions[1]);

  // All tracks have to be initialized before starting
  // All desired custom frame field names must be availiable starting from frame 0
  for (unsigned int frameNumber = 0; frameNumber < this->TrackedFrameList->GetNumberOfTrackedFrames(); frameNumber++)
  {
    PlusTrackedFrame* trackedFrame(NULL);
    trackedFrame = this->TrackedFrameList->GetTrackedFrame(frameNumber);
    std::vector<std::string> fieldNames;
    trackedFrame->GetCustomFrameFieldNameList(fieldNames);
    for (std::vector<std::string>::iterator it = fieldNames.begin(); it != fieldNames.end(); it++)
    {
      // Timestamp is already encoded in frames
      if (*it == "Timestamp")
      {
        continue;
      }

      uint64_t trackNumber = this->CustomFrameFieldTracks[*it];
      if (trackNumber <= 0)
      {
        int metaDataTrackNumber = this->MKVWriter->AddMetaDataTrack((*it));
        if (metaDataTrackNumber > 0)
        {
          this->CustomFrameFieldTracks[*it] = metaDataTrackNumber;
        }
      }
    }
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::AppendImagesToHeader()
{
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::WriteImages()
{
  // Do the actual writing
  if (!this->Initialized)
  {
    if (this->WriteInitialImageHeader() != PLUS_SUCCESS)
    {
      return PLUS_FAIL;
    }
    this->Initialized = true;
  }

  for (unsigned int frameNumber = 0; frameNumber < this->TrackedFrameList->GetNumberOfTrackedFrames(); frameNumber++)
  {
    PlusTrackedFrame* trackedFrame(NULL);

    if (this->EnableImageDataWrite)
    {
      trackedFrame = this->TrackedFrameList->GetTrackedFrame(frameNumber);
      if (trackedFrame == NULL)
      {
        LOG_ERROR("Cannot access frame " << frameNumber << " while trying to writing compress data into file");
        continue;
      }

      if (this->InitialTimestamp == -1)
      {
        this->InitialTimestamp = trackedFrame->GetTimestamp();
      }
      
      if (this->MKVWriter->EncodeAndWriteVideoFrame(trackedFrame->GetImageData()->GetImage(), this->VideoTrackNumber, trackedFrame->GetTimestamp() - this->InitialTimestamp)
        != PLUS_SUCCESS)
      {
        vtkErrorMacro("Could not encode and write frame to file!");
        continue;
      }

      std::map<std::string, std::string> customFields = trackedFrame->GetCustomFields();
      for (std::map<std::string, std::string>::iterator customFieldIt = customFields.begin(); customFieldIt != customFields.end(); ++customFieldIt)
      {
        if (customFieldIt->first == "Timestamp")
        {
          continue;
        }

        uint64_t trackID = this->CustomFrameFieldTracks[customFieldIt->first];
        if (trackID == 0)
        {
          vtkErrorMacro("Could not find metadata track for: " << customFieldIt->first);
          continue;
        }

        this->MKVWriter->WriteMetaData(customFieldIt->second, trackID, trackedFrame->GetTimestamp() - this->InitialTimestamp);
      }
    }
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::Close()
{
  this->MKVWriter->Close();
  this->MKVReader->Close();
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::WriteCompressedImagePixelsToFile(int& compressedDataSize)
{
  return this->WriteImages();
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::SetFileName(const std::string& aFilename)
{
  this->FileName = aFilename;
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::UpdateDimensionsCustomStrings(int numberOfFrames, bool isData3D)
{
  return PLUS_SUCCESS;
}