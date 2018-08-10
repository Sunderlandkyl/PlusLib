/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

#include "vtkPlusOpenIGTLinkCodec.h"

// VTK includes
#include <vtkLookupTable.h>
#include <vtkObjectFactory.h>
#include <vtkImageData.h>
#include <vtkUnsignedCharArray.h>

#include<igtlioVideoConverter.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkPlusOpenIGTLinkCodec);

//---------------------------------------------------------------------------
vtkPlusOpenIGTLinkCodec::vtkPlusOpenIGTLinkCodec()
  : GreyScaleToRGBFilter(vtkSmartPointer<vtkImageMapToColors>::New())
{
}

//---------------------------------------------------------------------------
vtkPlusOpenIGTLinkCodec::~vtkPlusOpenIGTLinkCodec()
{
}

//---------------------------------------------------------------------------
vtkSmartPointer<vtkUnsignedCharArray> vtkPlusOpenIGTLinkCodec::EncodeFrame(vtkImageData* inputFrame, unsigned long &size, int &frameType)
{

  if (!this->EncoderInitialized)
  {
    this->Encoder->SetPicWidthAndHeight(this->Dimensions[0], this->Dimensions[1]);
    
    this->Encoder->InitializeEncoder();
    this->EncoderInitialized = true;
  }
  this->Encoder->SetLosslessLink(false);

  igtl::VideoMessage::Pointer videoMessage = igtl::VideoMessage::New();
  igtlioVideoConverter::HeaderData headerData = igtlioVideoConverter::HeaderData();
  igtlioVideoConverter::ContentData contentData = igtlioVideoConverter::ContentData();
  contentData.videoMessage = videoMessage;
  contentData.image = inputFrame;

  if (inputFrame->GetNumberOfScalarComponents() != 3)
  {
    // TODO: handle greyscale
    if (!this->GreyScaleToRGBFilter->GetLookupTable())
    {
      vtkSmartPointer<vtkLookupTable> lookupTable = vtkSmartPointer<vtkLookupTable>::New();
      lookupTable->SetNumberOfTableValues(256);

      // Greyscale image
      if (inputFrame->GetNumberOfScalarComponents() == 1)
      {
        lookupTable->SetHueRange(0.0, 0.0);
        lookupTable->SetSaturationRange(0.0, 0.0);
        lookupTable->SetValueRange(0.0, 1.0);
        lookupTable->SetRange(0.0, 255.0);
        lookupTable->Build();
      }
      this->GreyScaleToRGBFilter->SetLookupTable(lookupTable);
    }

    this->GreyScaleToRGBFilter->SetInputData(inputFrame);
    this->GreyScaleToRGBFilter->SetOutputFormatToRGB();
    this->GreyScaleToRGBFilter->Update();
    contentData.image = this->GreyScaleToRGBFilter->GetOutput();
  }

  // TODO: encoding should be done without the use of the video message
  if (!igtlioVideoConverter::toIGTL(headerData, contentData, this->Encoder))
  {
    vtkErrorMacro("Could not create video message!");
    return false;
  }
  
  uint8_t* pointer = videoMessage->GetPackFragmentPointer(2);
  size = videoMessage->GetPackFragmentSize(2);

  //unsigned char* encodedFrame = new unsigned char[size];
  vtkSmartPointer<vtkUnsignedCharArray> encodedFrame = vtkSmartPointer<vtkUnsignedCharArray>::New();
  encodedFrame->Allocate(size);
  memcpy(encodedFrame->GetPointer(0), pointer, size);
  frameType = videoMessage->GetFrameType();

  return encodedFrame;
}

//---------------------------------------------------------------------------
bool vtkPlusOpenIGTLinkCodec::DecodeFrame(unsigned char* bitstream, unsigned long size, vtkImageData* outputFrame)
{
  outputFrame->SetDimensions(this->Dimensions[0], this->Dimensions[1], this->Dimensions[2]);
  outputFrame->AllocateScalars(VTK_UNSIGNED_CHAR, 3); // UNLESS GREYSCALE

  igtl_uint32 dimensions[2] = { this->Dimensions[0], this->Dimensions[1] };
  igtl_uint64 bitstreamSize = size;

  vtkSmartPointer<vtkImageData> yuvImage = vtkSmartPointer<vtkImageData>::New();
  yuvImage->SetDimensions(this->Dimensions[0], this->Dimensions[1] * 3 / 2, 1);
  yuvImage->AllocateScalars(VTK_UNSIGNED_CHAR, 1);

  this->Decoder->DecodeBitStreamIntoFrame(bitstream, (igtl_uint8*)yuvImage->GetScalarPointer(), dimensions, bitstreamSize);
  this->Decoder->ConvertYUVToRGB((igtl_uint8*)yuvImage->GetScalarPointer(), (igtl_uint8*)outputFrame->GetScalarPointer(), this->Dimensions[1], this->Dimensions[0]);
  return true;
}

//---------------------------------------------------------------------------
vtkSmartPointer<vtkGenericCodec> vtkPlusOpenIGTLinkCodec::CreateCodecInstance()
{
  vtkSmartPointer<vtkPlusOpenIGTLinkCodec> codec = vtkSmartPointer<vtkPlusOpenIGTLinkCodec>::New();
  codec->SetCodecFourCC(this->CodecFourCC);
  codec->Encoder = igtl::GenericEncoder::Pointer((igtl::GenericEncoder*)this->Encoder->CreateAnother().GetPointer());
  codec->Decoder = igtl::GenericDecoder::Pointer((igtl::GenericDecoder*)this->Decoder->CreateAnother().GetPointer());
  return codec;
}

//---------------------------------------------------------------------------
void vtkPlusOpenIGTLinkCodec::PrintSelf(ostream& os, vtkIndent indent)
{
  this->vtkObject::PrintSelf(os, indent);
}
