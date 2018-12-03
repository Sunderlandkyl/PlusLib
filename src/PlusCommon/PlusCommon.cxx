/*=Plus=header=begin======================================================
Program: Plus
Copyright (c) Laboratory for Percutaneous Surgery. All rights reserved.
See License.txt for details.
=========================================================Plus=header=end*/

// Local includes
#include "PlusConfigure.h"
#include "PlusCommon.h"
#include "PlusRevision.h"

// VTK includes
#include <vtkXMLDataElement.h>
#include <vtksys/SystemTools.hxx>

// STL includes
#include <algorithm>
#include <string>

#ifdef PLUS_USE_OpenIGTLink
// IGTL includes
#include <igtlImageMessage.h>
#endif

//----------------------------------------------------------------------------
std::string PlusCommon::GetPlusLibVersionString()
{
  std::string plusLibVersion = std::string("Plus-") + std::string(PLUSLIB_VERSION) + "." + std::string(PLUSLIB_SHORT_REVISION);
#ifdef _DEBUG
  plusLibVersion += " (debug build)";
#endif
#if defined _WIN64
  plusLibVersion += " - Win64";
#elif defined _WIN32
  plusLibVersion += " - Win32";
#elif defined __APPLE__
  plusLibVersion += " - Mac";
#else
  plusLibVersion += " - Linux";
#endif
  return plusLibVersion;
}

#ifdef PLUS_USE_OpenIGTLink
//----------------------------------------------------------------------------
// static
PlusCommon::VTKScalarPixelType PlusCommon::GetVTKScalarPixelTypeFromIGTL(PlusCommon::IGTLScalarPixelType igtlPixelType)
{
  switch (igtlPixelType)
  {
  case igtl::ImageMessage::TYPE_INT8:
    return VTK_CHAR;
  case igtl::ImageMessage::TYPE_UINT8:
    return VTK_UNSIGNED_CHAR;
  case igtl::ImageMessage::TYPE_INT16:
    return VTK_SHORT;
  case igtl::ImageMessage::TYPE_UINT16:
    return VTK_UNSIGNED_SHORT;
  case igtl::ImageMessage::TYPE_INT32:
    return VTK_INT;
  case igtl::ImageMessage::TYPE_UINT32:
    return VTK_UNSIGNED_INT;
  case igtl::ImageMessage::TYPE_FLOAT32:
    return VTK_FLOAT;
  case igtl::ImageMessage::TYPE_FLOAT64:
    return VTK_DOUBLE;
  default:
    return VTK_VOID;
  }
}

//----------------------------------------------------------------------------
// static
PlusCommon::IGTLScalarPixelType PlusCommon::GetIGTLScalarPixelTypeFromVTK(PlusCommon::VTKScalarPixelType pixelType)
{
  switch (pixelType)
  {
  case VTK_CHAR:
    return igtl::ImageMessage::TYPE_INT8;
  case VTK_UNSIGNED_CHAR:
    return igtl::ImageMessage::TYPE_UINT8;
  case VTK_SHORT:
    return igtl::ImageMessage::TYPE_INT16;
  case VTK_UNSIGNED_SHORT:
    return igtl::ImageMessage::TYPE_UINT16;
  case VTK_INT:
    return igtl::ImageMessage::TYPE_INT32;
  case VTK_UNSIGNED_INT:
    return igtl::ImageMessage::TYPE_UINT32;
  case VTK_FLOAT:
    return igtl::ImageMessage::TYPE_FLOAT32;
  case VTK_DOUBLE:
    return igtl::ImageMessage::TYPE_FLOAT64;
  default:
    // There is no unknown IGT scalar pixel type, so display an error message
    //**LOG_ERROR("Unknown conversion between VTK scalar pixel type (" << pixelType << ") and IGT pixel type - return igtl::ImageMessage::TYPE_INT8 by default!");
    return igtl::ImageMessage::TYPE_INT8;
  }
}

#endif
