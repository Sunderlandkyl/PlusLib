/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "vtkPlusMkvSequenceIO.h"

// PlusLib includes
#include "PlusTrackedFrame.h"
#include "vtkPlusTrackedFrameList.h"

// libwebm includes
#include "mkvmuxer/mkvwriter.h"
#include "mkvparser/mkvreader.h"

// IGTLIO includes
#include "igtlioVideoConverter.h"

// VTK includes
#include <vtkLookupTable.h>


// Track language identifiers
const char* MKV_TRACK_LANGUAGE_UNDEFINED = "und";

// Track codec identifiers
const char* MKV_TRACK_CODEC_SIMPLETEXT = "S_TEXT/UTF8";
const char* MKV_TRACK_CODEC_VP9 = "V_VP9";

// Track name prefixes
const char* MKV_TRACK_VIDEO_NAME_PREFIX = "Video_";

// Global metadata tag identifiers
const char* MKV_TAG_GREYSCALE = "GREYSCALE";

double NANOSECONDS_IN_SECOND = 1000000000.0;

//----------------------------------------------------------------------------

vtkStandardNewMacro(vtkPlusMkvSequenceIO);

//----------------------------------------------------------------------------
vtkPlusMkvSequenceIO::vtkPlusMkvSequenceIO()
  : vtkPlusSequenceIOBase()
  , VideoTrackNumber(-1)
  , Encoder(NULL)
  , Decoder(NULL)
  , EBMLHeader(NULL)
  , MKVWriter(NULL)
  , MKVWriteSegment(NULL)
  , MKVReader(NULL)
  , MKVReadSegment(NULL)
  , IsGreyScale(false)
  , GreyScaleToRGBFilter(vtkSmartPointer<vtkImageMapToColors>::New())
  , RGBToGreyScaleFilter(vtkSmartPointer<vtkImageMapToColors>::New())
{
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
  this->Close();

  if (!this->MKVReader)
  {
    this->MKVReader = new mkvparser::MkvReader();
  }
  this->MKVReader->Open(this->FileName.c_str());

  if (!this->EBMLHeader)
  {
    this->EBMLHeader = new mkvparser::EBMLHeader();
  }
  long long readerPosition;
  this->EBMLHeader->Parse(this->MKVReader, readerPosition);

  mkvparser::Segment::CreateInstance(this->MKVReader, readerPosition, this->MKVReadSegment);
  if (!this->MKVReadSegment)
  {
    LOG_ERROR("Could not read mkv segment!")
    this->Close();
    return PLUS_FAIL;
  }
  this->MKVReadSegment->Load();
  this->MKVReadSegment->ParseHeaders();

  const mkvparser::Tracks* tracks = this->MKVReadSegment->GetTracks();
  if (!tracks)
  {
    LOG_ERROR("Could not retrieve tracks!")
    this->Close();
    return PLUS_FAIL;
  }

  this->CustomFrameFieldTracks.clear();

  unsigned long numberOfTracks = tracks->GetTracksCount();
  for (unsigned long trackIndex = 0; trackIndex < numberOfTracks; ++trackIndex)
  {
    const mkvparser::Track* track = tracks->GetTrackByIndex(trackIndex);
    if (!track)
    {
      continue;
    }

    const long trackType = track->GetType();
    const long trackNumber = track->GetNumber();
    const unsigned long long trackUid = track->GetUid();

    if (trackType == mkvparser::Track::kVideo)
    {
      // We only support one video track, so only the parameters for the last video track will be stored
      const mkvparser::VideoTrack* const videoTrack = static_cast<const mkvparser::VideoTrack*>(track);
      this->VideoTrackNumber = trackNumber;

      this->Dimensions[0] = videoTrack->GetWidth();
      this->Dimensions[1] = videoTrack->GetHeight();
      this->Dimensions[2] = 1;
      this->FrameRate = videoTrack->GetFrameRate();

      if (videoTrack->GetCodecId())
      {
        this->EncodingType = videoTrack->GetCodecId();
      }
    }
    else if (trackType == mkvparser::Track::kMetadata || trackType == mkvparser::Track::kSubtitle)
    {
      std::string trackName = track->GetNameAsUTF8();
      long trackNumber = track->GetNumber();
      this->CustomFrameFieldTracks[trackName] = trackNumber;
    }
  }

  this->IsGreyScale = false;
  const mkvparser::Tags* tags = this->MKVReadSegment->GetTags();
  for (int i = 0; tags && i < tags->GetTagCount(); ++i)
  {
    const mkvparser::Tags::Tag* tag = tags->GetTag(i);
    for (int j = 0; tag && j < tag->GetSimpleTagCount(); ++j)
    {
      const mkvparser::Tags::SimpleTag* simpleTag = tag->GetSimpleTag(j);
      if (simpleTag && simpleTag->GetTagName() && simpleTag->GetTagString())
      {
        std::string tagName = simpleTag->GetTagName();
        std::string tagValue = simpleTag->GetTagString();
        if (tagName == MKV_TAG_GREYSCALE)
        {
          this->IsGreyScale = (tagValue == "1" ? true : false);
        }
      }
    }
  }

  if (this->EncodingType == MKV_TRACK_CODEC_VP9)
  {
    this->Decoder = igtl::VP9Decoder::New();
  }
  else
  {
    LOG_ERROR("Could not find matching codec " << this->EncodingType);
    this->Close();
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
// Read the spacing and dimensions of the image.
PlusStatus vtkPlusMkvSequenceIO::ReadImagePixels()
{
  if (this->VideoTrackNumber <= 0)
  {
    LOG_ERROR("Could not find a video track");
    this->Close();
    return PLUS_FAIL;
  }

  const mkvparser::Tracks* tracks = this->MKVReadSegment->GetTracks();

  double lastTimestamp = -1;

  vtkSmartPointer<vtkImageData> yuvImage = vtkSmartPointer<vtkImageData>::New();
  yuvImage->SetDimensions(this->Dimensions[0], this->Dimensions[1] * 3/2 , 1);
  yuvImage->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

  vtkSmartPointer<vtkImageData> rgbImage = vtkSmartPointer<vtkImageData>::New();
  rgbImage->SetExtent(0, this->Dimensions[0] - 1,
                      0, this->Dimensions[1] - 1,
                      0, this->Dimensions[2] - 1);
  rgbImage->SetOrigin(0, 0, 0);
  rgbImage->AllocateScalars(VTK_UNSIGNED_CHAR, 3);

  unsigned int totalFrameNumber = 0;
  const mkvparser::Cluster* cluster = this->MKVReadSegment->GetFirst();
  while (cluster != NULL && !cluster->EOS())
  {
    const long long timeCode = cluster->GetTimeCode();
    const long long timestampNanoSeconds = cluster->GetTime();
    // Convertitn nanoseconds to seconds
    double timestampSeconds = timestampNanoSeconds / NANOSECONDS_IN_SECOND; // Timestamp of the current cluster in seconds

    const mkvparser::BlockEntry* blockEntry;
    long status = cluster->GetFirst(blockEntry);
    if (status < 0)
    {
      LOG_ERROR("Could not find block!");
      this->Close();
      return PLUS_FAIL;
    }

    while (blockEntry != NULL && !blockEntry->EOS())
    {
      const mkvparser::Block* const block = blockEntry->GetBlock();
      unsigned long trackNum = static_cast<unsigned long>(block->GetTrackNumber());
      const mkvparser::Track* const track = tracks->GetTrackByNumber(trackNum);

      if (!track)
      {
        LOG_ERROR("Could not find track!");
        this->Close();
        return PLUS_FAIL;
      }

      const char* trackName = track->GetNameAsUTF8();
      const long long trackType = track->GetType();
      const int frameCount = block->GetFrameCount();
      const long long time_ns = block->GetTime(cluster);
      const long long discard_padding = block->GetDiscardPadding();

      for (int i = 0; i < frameCount; ++i)
      {
        const mkvparser::Block::Frame& frame = block->GetFrame(i);
        const long size = frame.len;
        const long long offset = frame.pos;

        unsigned char* bitstream = new unsigned char[size];
        frame.Read(this->MKVReader, bitstream);

        // Handle video frame
        if (trackNum == this->VideoTrackNumber)
        {
          this->CreateTrackedFrameIfNonExisting(totalFrameNumber);

          // TODO: Not all files seem to be encoded with either timestamp or framerate
          if (lastTimestamp != -1 && lastTimestamp == timestampSeconds)
          {
            if (this->FrameRate > 0.0)
            {
              timestampSeconds = lastTimestamp + (1. / this->FrameRate);
            }
            else
            {
              // Assuming 25 fps
              timestampSeconds = lastTimestamp + (1./25.);
            }
          }
          lastTimestamp = timestampSeconds;

          FrameSizeType frameSize = { this->Dimensions[0], Dimensions[1], 1 };
          PlusTrackedFrame* trackedFrame = this->TrackedFrameList->GetTrackedFrame(totalFrameNumber);
          trackedFrame->GetImageData()->SetImageOrientation(US_IMG_ORIENT_MF); // TODO: save orientation and type
          trackedFrame->GetImageData()->SetImageType(US_IMG_RGB_COLOR);
          trackedFrame->SetTimestamp(timestampSeconds);
          if (this->IsGreyScale)
          {
            trackedFrame->GetImageData()->AllocateFrame(frameSize, VTK_UNSIGNED_CHAR, 1);
          }
          else
          {
            trackedFrame->GetImageData()->AllocateFrame(frameSize, VTK_UNSIGNED_CHAR, 3);
          }

          if (this->ConvertBitstreamToTrackedFrame(bitstream, size, yuvImage, rgbImage, trackedFrame) == PLUS_SUCCESS)
          {
            totalFrameNumber++;
          }
        }
        else if (trackType == mkvparser::Track::kMetadata || trackType == mkvparser::Track::kSubtitle)
        {
          PlusTrackedFrame* trackedFrame;

          // Find the tracked frame with the same timestamp
          for (unsigned int frameIndex = 0; frameIndex < this->TrackedFrameList->GetNumberOfTrackedFrames(); ++frameIndex)
          {
            PlusTrackedFrame* currentFrame = this->TrackedFrameList->GetTrackedFrame(frameIndex);
            if (currentFrame->GetTimestamp() == timestampSeconds)
            {
              trackedFrame = currentFrame;
              break;
            }
          }

          // Apply field value to frame with matching timestamp
          if (trackedFrame)
          {
            trackedFrame->SetCustomFrameField(trackName, (char*)bitstream);
          }
        }

        delete[] bitstream;
        bitstream = NULL;
      }

      status = cluster->GetNext(blockEntry, blockEntry);
      if (status < 0)  // error
      {
        this->Close();
        return PLUS_FAIL;
      }
    }
    cluster = this->MKVReadSegment->GetNext(cluster);
  }

  this->Close();
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::ConvertBitstreamToTrackedFrame(unsigned char* bitstream, long bistreamSize,
                                                                vtkImageData* yuvImage, vtkImageData* rgbImage,
                                                                PlusTrackedFrame* outputTrackedFrame)
{

  igtl_uint32 dimensions[2] = { this->Dimensions[0], this->Dimensions[1] };
  igtl_uint64 memSize = bistreamSize;
  if (this->Decoder->DecodeBitStreamIntoFrame(bitstream, (uint8_t*)yuvImage->GetScalarPointer(), dimensions, memSize) > 0)
  {
    yuvImage->Modified();
    GenericDecoder::ConvertYUVToRGB((uint8_t*)yuvImage->GetScalarPointer(), (uint8_t*)rgbImage->GetScalarPointer(), this->Dimensions[1], this->Dimensions[0]);
    rgbImage->Modified();

    if (this->IsGreyScale)
    {
      if (!this->RGBToGreyScaleFilter->GetLookupTable())
      {
        vtkSmartPointer<vtkLookupTable> lookupTable = vtkSmartPointer<vtkLookupTable>::New();
        lookupTable->SetNumberOfTableValues(256);
        lookupTable->SetHueRange(0.0, 0.0);
        lookupTable->SetSaturationRange(0.0, 0.0);
        lookupTable->SetValueRange(0.0, 1.0);
        lookupTable->SetRange(0.0, 255.0);
        lookupTable->Build();
        this->RGBToGreyScaleFilter->SetLookupTable(lookupTable);
      }
      this->RGBToGreyScaleFilter->SetInputData(rgbImage);
      this->RGBToGreyScaleFilter->SetOutputFormatToLuminance();
      this->RGBToGreyScaleFilter->Update();
      outputTrackedFrame->GetImageData()->GetImage()->DeepCopy(this->RGBToGreyScaleFilter->GetOutput());
    }
    else
    {
      outputTrackedFrame->GetImageData()->GetImage()->DeepCopy(rgbImage);
    }
  }
  else
  {
    LOG_ERROR("Could not decode frame!");
    return PLUS_FAIL;
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::PrepareImageFile()
{
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
  this->Close();

  this->MKVWriter = new mkvmuxer::MkvWriter();
  if (!this->MKVWriter->Open(this->FileName.c_str()))
  {
    LOG_ERROR("Could not open file " << this->FileName << "!");
    return PLUS_FAIL;
  }

  if (!this->EBMLHeader)
  {
    this->EBMLHeader = new mkvparser::EBMLHeader();
  }
  this->EBMLHeader->Init();

  if (!this->MKVWriteSegment)
  {
    this->MKVWriteSegment = new mkvmuxer::Segment();
  }

  if (!this->MKVWriteSegment->Init(this->MKVWriter))
  {
    LOG_ERROR("Could not initialize MKV file segment!");
    return PLUS_FAIL;
  }

  this->MKVWriteSegment->OutputCues(true);
  this->MKVWriteSegment->AccurateClusterDuration(true);
  this->MKVWriteSegment->UseFixedSizeClusterTimecode(true);

  mkvmuxer::SegmentInfo* const info = this->MKVWriteSegment->GetSegmentInfo();
  info->set_timecode_scale(1000); // TODO: magic numbers, look up in documentation
  info->set_duration(2.345); // TODO: magic numbers, look up in documentation

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::AppendImagesToHeader()
{
  return this->WriteImages();
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::WriteImages()
{
  if (this->TrackedFrameList->GetNumberOfTrackedFrames() < 1)
  {
    return PLUS_SUCCESS;
  }

  if (!this->MKVWriter)
  {
    this->WriteInitialImageHeader();
  }

  this->InitializeTracks();

  // Do the actual writing

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

      igtl::VideoMessage::Pointer videoMessage = igtl::VideoMessage::New();
      igtlioVideoConverter::HeaderData headerData = igtlioVideoConverter::HeaderData();
      igtlioVideoConverter::ContentData contentData = igtlioVideoConverter::ContentData();
      contentData.videoMessage = videoMessage;

      vtkSmartPointer<vtkImageData> frameImage = trackedFrame->GetImageData()->GetImage();
      if (frameImage->GetNumberOfScalarComponents() != 3)
      {
        if (!this->GreyScaleToRGBFilter->GetLookupTable())
        {
          vtkSmartPointer<vtkLookupTable> lookupTable = vtkSmartPointer<vtkLookupTable>::New();
          lookupTable->SetNumberOfTableValues(256);

          // Greyscale image
          if (frameImage->GetNumberOfScalarComponents() == 1)
          {
            lookupTable->SetHueRange(0.0, 0.0);
            lookupTable->SetSaturationRange(0.0, 0.0);
            lookupTable->SetValueRange(0.0, 1.0);
            lookupTable->SetRange(0.0, 255.0);
            lookupTable->Build();
          }
          this->GreyScaleToRGBFilter->SetLookupTable(lookupTable);
        }

        this->GreyScaleToRGBFilter->SetInputData(frameImage);
        this->GreyScaleToRGBFilter->SetOutputFormatToRGB();
        this->GreyScaleToRGBFilter->Update();
        frameImage = this->GreyScaleToRGBFilter->GetOutput();
      }

      contentData.image = frameImage;

      // TODO: encoding should be done without the use of the video message
      if (!igtlioVideoConverter::toIGTL(headerData, contentData, this->Encoder))
      {
        LOG_ERROR("Could not create video message!");
        return PLUS_FAIL;
      }

      uint8_t* pointer = videoMessage->GetPackFragmentPointer(2);
      int size = videoMessage->GetPackFragmentSize(2);

      uint64_t timestampNanoSeconds = std::floor(NANOSECONDS_IN_SECOND * (trackedFrame->GetTimestamp() - this->InitialTimestampSeconds));
      this->MKVWriteSegment->AddFrame(pointer, size, this->VideoTrackNumber, timestampNanoSeconds, videoMessage->GetFrameType() == VideoFrameType::FrameTypeKey);

      std::vector<std::string> fieldNames;
      trackedFrame->GetCustomFrameFieldNameList(fieldNames);
      for (std::vector<std::string>::iterator it = fieldNames.begin(); it != fieldNames.end(); it++)
      {
        // Timestamp is already encoded in frames
        if (*it == "Timestamp")
        {
          continue;
        }

        // Additional frame field tracks, including transforms, status, etc.
        uint64_t metadataTrackNumber = this->CustomFrameFieldTracks[*it];
        if (metadataTrackNumber > 0)
        {
          if (trackedFrame->GetCustomFrameField(*it))
          {
            std::string metaDataString = trackedFrame->GetCustomFrameField(*it);
            this->MKVWriteSegment->AddMetadata(
              (uint8_t*)metaDataString.c_str(), sizeof(char) * (metaDataString.length() + 1),
              metadataTrackNumber, timestampNanoSeconds, 1);
          }
        }
      }
      this->MKVWriteSegment->AddCuePoint(timestampNanoSeconds, this->VideoTrackNumber);
    }
  }

  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::InitializeTracks()
{
  if (this->VideoTrackNumber > 0)
  {
    // Tracks already initialized
    return PLUS_SUCCESS;
  }

  int* dimensions = this->TrackedFrameList->GetTrackedFrame(0)->GetImageData()->GetImage()->GetDimensions();
  this->Dimensions[0] = dimensions[0];
  this->Dimensions[1] = dimensions[1];
  this->Dimensions[2] = dimensions[2];

  // If the image only has one component, then it is considered a greyscale image
  // The image is converted to a greyscale RGB image for the purpose of compression
  // When the image is read back, this tag will 
  mkvmuxer::Tag* greyScaleTag = this->MKVWriteSegment->AddTag();
  if (!greyScaleTag)
  {
    LOG_ERROR("Could not create tag in segment!");
    return PLUS_FAIL;
  }

  unsigned int numberOfScalarComponents = 0;
  if (!this->TrackedFrameList->GetTrackedFrame(0)->GetNumberOfScalarComponents(numberOfScalarComponents))
  {
    LOG_ERROR("Could not get number of scalar components from frame 0!");
    return PLUS_FAIL;
  }
  std::string isGreyScaleString = numberOfScalarComponents != 3 ? "1" : "0";
  greyScaleTag->add_simple_tag(MKV_TAG_GREYSCALE, isGreyScaleString.c_str());

  this->InitialTimestampSeconds = this->TrackedFrameList->GetTrackedFrame(0)->GetTimestamp();

  this->VideoTrackNumber = this->MKVWriteSegment->AddVideoTrack(dimensions[0], dimensions[1], 0);
  if (this->VideoTrackNumber <= 0)
  {
    LOG_ERROR("Could not create video track!");
    return PLUS_FAIL;
  }

  mkvmuxer::VideoTrack* videoTrack = (mkvmuxer::VideoTrack*)this->MKVWriteSegment->GetTrackByNumber(this->VideoTrackNumber);
  if (!videoTrack)
  {
    LOG_ERROR("Could not find video track: " << this->VideoTrackNumber << "!");
    return PLUS_FAIL;
  }

  std::stringstream videoTrackNameSS;
  videoTrackNameSS << MKV_TRACK_VIDEO_NAME_PREFIX << this->VideoTrackNumber;
  std::string videoTrackName = videoTrackNameSS.str();

  videoTrack->set_codec_id(MKV_TRACK_CODEC_VP9); // TODO: variable codec types
  videoTrack->set_name(videoTrackName.c_str());
  videoTrack->set_frame_rate(0); // TODO: magic number --> was in the sample (units?)
  videoTrack->set_language(MKV_TRACK_LANGUAGE_UNDEFINED);
  this->MKVWriteSegment->CuesTrack(this->VideoTrackNumber);

  if (!this->Encoder)
  {
    this->Encoder = igtl::VP9Encoder::New();
    this->Encoder->SetPicWidthAndHeight(this->Dimensions[0], this->Dimensions[1]);
    this->Encoder->SetLosslessLink(false);
    this->Encoder->InitializeEncoder();
  }

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
        mkvmuxer::Track* const track = this->MKVWriteSegment->AddTrack(0);
        if (track)
        {
          track->set_name((*it).c_str());
          track->set_type(mkvparser::Track::kSubtitle);
          track->set_codec_id(MKV_TRACK_CODEC_SIMPLETEXT);
          track->set_language(MKV_TRACK_LANGUAGE_UNDEFINED);
          this->CustomFrameFieldTracks[*it] = track->number();
        }
        else
        {
          continue;
        }
      }
    }
  }
  return PLUS_SUCCESS;
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::WriteCompressedImagePixelsToFile(int& compressedDataSize)
{
  return this->WriteImages();
}

//----------------------------------------------------------------------------
PlusStatus vtkPlusMkvSequenceIO::Close()
{
  if (this->MKVWriteSegment)
  {
    this->MKVWriteSegment->Finalize();
  }

  if (this->MKVWriter)
  {
    this->MKVWriter->Close();
    delete this->MKVWriter;
    this->MKVWriter = NULL;
  }

  if (this->MKVWriteSegment)
  {
    delete this->MKVWriteSegment;
    this->MKVWriteSegment = NULL;
  }


  if (this->MKVReader)
  {
    this->MKVReader->Close();
  }

  if (this->MKVReadSegment)
  {
    delete this->MKVReadSegment;
    this->MKVReadSegment = NULL;
  }

  this->Encoder = NULL;
  this->Decoder = NULL;
  this->VideoTrackNumber = -1;
  this->CustomFrameFieldTracks.clear();

  return PLUS_SUCCESS;
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