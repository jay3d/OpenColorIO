/*
Copyright (c) 2003-2010 Sony Pictures Imageworks Inc., et al.
All Rights Reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
* Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.
* Neither the name of Sony Pictures Imageworks nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <OpenColorIO/OpenColorIO.h>

#include "Mutex.h"
#include "OpBuilders.h"
#include "PathUtils.h"
#include "Processor.h"
#include "pystring/pystring.h"
#include "tinyxml/tinyxml.h"

#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

OCIO_NAMESPACE_ENTER
{
    namespace
    {
        // These are the 709 primaries specified by the ASC.
        const float DEFAULT_LUMA_COEFF_R = 0.2126f;
        const float DEFAULT_LUMA_COEFF_G = 0.7152f;
        const float DEFAULT_LUMA_COEFF_B = 0.0722f;
    }
    
    
    ///////////////////////////////////////////////////////////////////////////
    
    
    
    namespace
    {
        ConstConfigRcPtr g_currentConfig;
        Mutex g_currentConfigLock;
    }
    
    ConstConfigRcPtr GetCurrentConfig()
    {
        AutoMutex lock(g_currentConfigLock);
        
        if(!g_currentConfig)
        {
            g_currentConfig = Config::CreateFromEnv();
        }
        
        return g_currentConfig;
    }
    
    void SetCurrentConfig(const ConstConfigRcPtr & config)
    {
        AutoMutex lock(g_currentConfigLock);
        
        g_currentConfig = config->createEditableCopy();
    }
    
    
    typedef std::vector<ColorSpaceRcPtr> ColorSpacePtrVec;
    typedef std::vector< std::pair<std::string, std::string> > RoleVec; // (lowercase role name, colorspace)
    typedef std::vector<std::string> DisplayKey; // (device, name, colorspace)
    
    class Config::Impl
    {
    public:
        std::string originalFileDir_;
        std::string resourcePath_;
        std::string resolvedResourcePath_;
        
        std::string description_;
        
        ColorSpacePtrVec colorspaces_;
        
        RoleVec roleVec_;
        
        std::vector<DisplayKey> displayDevices_;
        
        float defaultLumaCoefs_[3];
        
        Impl()
        {
            defaultLumaCoefs_[0] = DEFAULT_LUMA_COEFF_R;
            defaultLumaCoefs_[1] = DEFAULT_LUMA_COEFF_G;
            defaultLumaCoefs_[2] = DEFAULT_LUMA_COEFF_B;
        }
        
        ~Impl()
        {
        
        }
        
        Impl& operator= (const Impl & rhs)
        {
            resourcePath_ = rhs.resourcePath_;
            originalFileDir_ = rhs.originalFileDir_;
            resolvedResourcePath_ = rhs.resolvedResourcePath_;
            description_ = rhs.description_;
            
            colorspaces_.clear();
            colorspaces_.reserve(rhs.colorspaces_.size());
            
            for(unsigned int i=0; i<rhs.colorspaces_.size(); ++i)
            {
                colorspaces_.push_back(rhs.colorspaces_[i]->createEditableCopy());
            }
            
            roleVec_ = rhs.roleVec_; // Vector assignment operator will suffice for this
            displayDevices_ = rhs.displayDevices_; // Vector assignment operator will suffice for this
            
            memcpy(defaultLumaCoefs_, rhs.defaultLumaCoefs_, 3*sizeof(float));
            
            return *this;
        }
    };
    
    
    ///////////////////////////////////////////////////////////////////////////
    
    ConfigRcPtr Config::Create()
    {
        return ConfigRcPtr(new Config(), &deleter);
    }
    
    void Config::deleter(Config* c)
    {
        delete c;
    }
    
    ConstConfigRcPtr Config::CreateFromEnv()
    {
        char * file = std::getenv("OCIO");
        if(!file)
        {
            std::ostringstream os;
            os << "'OCIO' environment variable not set. ";
            os << "Please specify a valid OpenColorIO (.ocio) configuration file.";
            throw Exception(os.str().c_str());
        }
        
        return CreateFromFile(file);
    }
    
    
    
    ///////////////////////////////////////////////////////////////////////////
    
    
    
    Config::Config()
    : m_impl(new Config::Impl)
    {
    }
    
    Config::~Config()
    {
    }
    
    ConfigRcPtr Config::createEditableCopy() const
    {
        ConfigRcPtr config = Config::Create();
        *config->m_impl = *m_impl;
        return config;
    }
    
    const char * Config::getResourcePath() const
    {
        return m_impl->resourcePath_.c_str();
    }
    
    void Config::setResourcePath(const char * path)
    {
        m_impl->resourcePath_ = path;
        m_impl->resolvedResourcePath_ = path::join(m_impl->originalFileDir_, m_impl->resourcePath_);
    }
    
    const char * Config::getResolvedResourcePath() const
    {
        return m_impl->resolvedResourcePath_.c_str();
    }
    
    const char * Config::getDescription() const
    {
        return m_impl->description_.c_str();
    }
    
    void Config::setDescription(const char * description)
    {
        m_impl->description_ = description;
    }
    
    
    
    ///////////////////////////////////////////////////////////////////////////
    
    int Config::getNumColorSpaces() const
    {
        return static_cast<int>(m_impl->colorspaces_.size());
    }
    
    const char * Config::getColorSpaceNameByIndex(int index) const
    {
        if(index<0 || index >= (int)m_impl->colorspaces_.size())
        {
            return 0x0;
        }
        
        return m_impl->colorspaces_[index]->getName();
    }
    
    ConstColorSpaceRcPtr Config::getColorSpace(const char * name) const
    {
        int index = getIndexForColorSpace(name);
        if(index<0 || index >= (int)m_impl->colorspaces_.size())
        {
            return ColorSpaceRcPtr();
        }
        
        return m_impl->colorspaces_[index];
    }
    
    ColorSpaceRcPtr Config::getEditableColorSpace(const char * name)
    {
        int index = getIndexForColorSpace(name);
        if(index<0 || index >= (int)m_impl->colorspaces_.size())
        {
            return ColorSpaceRcPtr();
        }
        
        return m_impl->colorspaces_[index];
    }
    
    int Config::getIndexForColorSpace(const char * name) const
    {
        if(!name) return -1;
        
        std::string strname = name;
        if(strname.empty()) return -1;
        
        // Check to see if the name is a color space
        for(unsigned int index = 0; index < m_impl->colorspaces_.size(); ++index)
        {
            if(strname == m_impl->colorspaces_[index]->getName())
                return index;
        }
        
        // Check to see if the name is a role
        // The rolevec is all lowercase, so we must edit the name accordingly.
        
        std::string namelower = pystring::lower(strname);
        for(unsigned int i=0; i<m_impl->roleVec_.size(); ++i)
        {
            if(m_impl->roleVec_[i].first != namelower) continue;
            
            std::string csname = m_impl->roleVec_[i].second;
            
            for(unsigned int csi = 0; csi < m_impl->colorspaces_.size(); ++csi)
            {
                if(csname == m_impl->colorspaces_[csi]->getName())
                    return csi;
            }
            
            // TODO: Exception instead?  Specified role is found,
            // but referenced colorspace does not exist.
            return -1;
        }
        
        return -1;
    }
    
    void Config::addColorSpace(const ConstColorSpaceRcPtr & original)
    {
        ColorSpaceRcPtr cs = original->createEditableCopy();
        
        std::string name = cs->getName();
        if(name.empty())
            throw Exception("Cannot addColorSpace with an empty name.");
        
        // Check to see if the colorspace already exists
        for(unsigned int index = 0; index < m_impl->colorspaces_.size(); ++index)
        {
            if(name == m_impl->colorspaces_[index]->getName())
            {
                m_impl->colorspaces_[index] = cs;
                return;
            }
        }
        
        // Otherwise, add it
        m_impl->colorspaces_.push_back( cs );
    }
    
    void Config::clearColorSpaces()
    {
        m_impl->colorspaces_.clear();
    }
    
    
    
    
    
    
    const char * Config::parseColorSpaceFromString(const char * str) const
    {
        // Search the entire filePath, including directory name (if provided)
        // convert the filename to lowercase.
        std::string fullstr = pystring::lower(std::string(str));
        
        // See if it matches a lut name.
        // This is the position of the RIGHT end of the colorspace substring, not the left
        int rightMostColorPos=-1;
        std::string rightMostColorspace = "";
        int rightMostColorSpaceIndex = -1;
        
        // Find the right-most occcurance within the string for each colorspace.
        for (unsigned int i=0; i<m_impl->colorspaces_.size(); ++i)
        {
            std::string csname = pystring::lower(m_impl->colorspaces_[i]->getName());
            
            // find right-most extension matched in filename
            int colorspacePos = pystring::rfind(fullstr, csname);
            if(colorspacePos < 0)
                continue;
            
            // If we have found a match, move the pointer over to the right end of the substring
            // This will allow us to find the longest name that matches the rightmost colorspace
            colorspacePos += (int)csname.size();
            
            if ( (colorspacePos > rightMostColorPos) ||
                 ((colorspacePos == rightMostColorPos) && (csname.size() > rightMostColorspace.size()))
                )
            {
                rightMostColorPos = colorspacePos;
                rightMostColorspace = csname;
                rightMostColorSpaceIndex = i;
            }
        }
        
        if(rightMostColorSpaceIndex<0) return 0x0;
        return m_impl->colorspaces_[rightMostColorSpaceIndex]->getName();
    }
    
    
    // Roles
    void Config::setRole(const char * role, const char * colorSpaceName)
    {
        std::string rolelower = pystring::lower(role);
        
        // Set the role
        if(colorSpaceName)
        {
            for(unsigned int i=0; i<m_impl->roleVec_.size(); ++i)
            {
                if(m_impl->roleVec_[i].first == rolelower)
                {
                    m_impl->roleVec_[i].second = colorSpaceName;
                    return;
                }
            }
            m_impl->roleVec_.push_back( std::make_pair(rolelower, std::string(colorSpaceName) ) );
        }
        // Unset the role
        else
        {
            for(RoleVec::iterator iter = m_impl->roleVec_.begin();
                iter != m_impl->roleVec_.end();
                ++iter)
            {
                if(iter->first == rolelower)
                {
                    m_impl->roleVec_.erase(iter);
                    return;
                }
            }
        }
    }
    
    int Config::getNumRoles() const
    {
        return static_cast<int>(m_impl->roleVec_.size());
    }
    
    const char * Config::getRoleNameByIndex(int index) const
    {
        if(index<0 || index >= (int)m_impl->roleVec_.size())
        {
            return 0x0;
        }
        
        return m_impl->roleVec_[index].first.c_str();
    }
    
    
    // Display Transforms
    
    ///////////////////////////////////////////////////////////////////////////
    //
    // TODO: Use maps rather than vectors as storage for display options
    // These implementations suck in terms of scalability.
    // (If you had 100s of display devices, this would be criminally inefficient.)
    // But this can be ignored in the short-term, and doing so makes serialization
    // much simpler.
    //
    // (Alternatively, an ordered map would solve the issue too).
    //
    // m_displayDevices = [(device, name, colorspace),...]
    
    int Config::getNumDisplayDeviceNames() const
    {
        std::set<std::string> devices;
        
        for(unsigned int i=0; i<m_impl->displayDevices_.size(); ++i)
        {
            if(m_impl->displayDevices_[i].size() != 3) continue;
            devices.insert( m_impl->displayDevices_[i][0] );
        }
        
        return static_cast<int>(devices.size());
    }
    
    const char * Config::getDisplayDeviceName(int index) const
    {
        std::set<std::string> devices;
        
        for(unsigned int i=0; i<m_impl->displayDevices_.size(); ++i)
        {
            if(m_impl->displayDevices_[i].size() != 3) continue;
            devices.insert( m_impl->displayDevices_[i][0] );
            
            if((int)devices.size()-1 == index)
            {
                return m_impl->displayDevices_[i][0].c_str();
            }
        }
        
        std::ostringstream os;
        os << "Could not find display device with the specified index ";
        os << index << ".";
        throw Exception(os.str().c_str());
    }
    
    const char * Config::getDefaultDisplayDeviceName() const
    {
        if(getNumDisplayDeviceNames()>=1)
        {
            return getDisplayDeviceName(0);
        }
        
        return "";
    }
    
    int Config::getNumDisplayTransformNames(const char * device) const
    {
        std::set<std::string> names;
        
        for(unsigned int i=0; i<m_impl->displayDevices_.size(); ++i)
        {
            if(m_impl->displayDevices_[i].size() != 3) continue;
            if(m_impl->displayDevices_[i][0] != device) continue;
            names.insert( m_impl->displayDevices_[i][1] );
        }
        
        return static_cast<int>(names.size());
    }
    
    const char * Config::getDisplayTransformName(const char * device, int index) const
    {
        std::set<std::string> names;
        
        for(unsigned int i=0; i<m_impl->displayDevices_.size(); ++i)
        {
            if(m_impl->displayDevices_[i].size() != 3) continue;
            if(m_impl->displayDevices_[i][0] != device) continue;
            names.insert( m_impl->displayDevices_[i][1] );
            
            if((int)names.size()-1 == index)
            {
                return m_impl->displayDevices_[i][1].c_str();
            }
        }
        
        std::ostringstream os;
        os << "Could not find display colorspace for device ";
        os << device << " with the specified index ";
        os << index << ".";
        throw Exception(os.str().c_str());
    }
    
    const char * Config::getDefaultDisplayTransformName(const char * device) const
    {
        if(getNumDisplayTransformNames(device)>=1)
        {
            return getDisplayTransformName(device, 0);
        }
        
        return "";
    }
    
    const char * Config::getDisplayColorSpaceName(const char * device, const char * displayTransformName) const
    {
        for(unsigned int i=0; i<m_impl->displayDevices_.size(); ++i)
        {
            if(m_impl->displayDevices_[i].size() != 3) continue;
            if(m_impl->displayDevices_[i][0] != device) continue;
            if(m_impl->displayDevices_[i][1] != displayTransformName) continue;
            return m_impl->displayDevices_[i][2].c_str();
        }
        
        std::ostringstream os;
        os << "Could not find display colorspace for device ";
        os << device << " with the specified transform name ";
        os << displayTransformName << ".";
        throw Exception(os.str().c_str());
    }
    
    void Config::addDisplayDevice(const char * device,
                                        const char * displayTransformName,
                                        const char * csname)
    {
        // Is this device / display already registered?
        // If so, set it to the potentially new value.
        
        for(unsigned int i=0; i<m_impl->displayDevices_.size(); ++i)
        {
            if(m_impl->displayDevices_[i].size() != 3) continue;
            if(m_impl->displayDevices_[i][0] != device) continue;
            if(m_impl->displayDevices_[i][1] != displayTransformName) continue;
            
            m_impl->displayDevices_[i][2] = csname;
            return;
        }
        
        // Otherwise, add a new entry!
        DisplayKey displayKey;
        displayKey.push_back(std::string(device));
        displayKey.push_back(std::string(displayTransformName));
        displayKey.push_back(std::string(csname));
        m_impl->displayDevices_.push_back(displayKey);
    }
    
    void Config::getDefaultLumaCoefs(float * c3) const
    {
        memcpy(c3, m_impl->defaultLumaCoefs_, 3*sizeof(float));
    }
    
    void Config::setDefaultLumaCoefs(const float * c3)
    {
        memcpy(m_impl->defaultLumaCoefs_, c3, 3*sizeof(float));
    }
    
    ConstProcessorRcPtr Config::getProcessor(const ConstColorSpaceRcPtr & src,
                                             const ConstColorSpaceRcPtr & dst) const
    {
        if(!src)
        {
            throw Exception("Config::GetProcessor failed. Source colorspace is null.");
        }
        if(!dst)
        {
            throw Exception("Config::GetProcessor failed. Destination colorspace is null.");
        }
        
        LocalProcessorRcPtr processor = LocalProcessor::Create();
        processor->addColorSpaceConversion(*this, src, dst);
        processor->finalize();
        return processor;
    }
    
    //! Names can be colorspace name or role name
    ConstProcessorRcPtr Config::getProcessor(const char * srcName,
                                             const char * dstName) const
    {
        // TODO: Confirm !RcPtr works
        ConstColorSpaceRcPtr src = getColorSpace(srcName);
        if(!src)
        {
            std::ostringstream os;
            os << "Could not find colorspace '" << srcName << ".";
            throw Exception(os.str().c_str());
        }
        
        ConstColorSpaceRcPtr dst = getColorSpace(dstName);
        if(!dst)
        {
            std::ostringstream os;
            os << "Could not find colorspace '" << dstName << ".";
            throw Exception(os.str().c_str());
        }
        
        return getProcessor(src, dst);
    }
    
    ConstProcessorRcPtr Config::getProcessor(const ConstTransformRcPtr& transform,
                                             TransformDirection direction) const
    {
        LocalProcessorRcPtr processor = LocalProcessor::Create();
        processor->addTransform(*this, transform, direction);
        processor->finalize();
        return processor;
    }
    
    std::ostream& operator<< (std::ostream& os, const Config& config)
    {
        config.writeXML(os);
        return os;
    }
    
    
    
    
    
    
    
    
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //
    //
    //
    //
    //  Serialization
    
    
    namespace
    {
        ///////////////////////////////////////////////////////////////////////
        //
        // FileTransform
        
        FileTransformRcPtr CreateFileTransform(const TiXmlElement * element)
        {
            if(!element)
                throw Exception("CreateFileTransform received null XmlElement.");
            
            if(std::string(element->Value()) != "file")
            {
                std::ostringstream os;
                os << "HandleElement passed incorrect element type '";
                os << element->Value() << "'. ";
                os << "Expected 'file'.";
                throw Exception(os.str().c_str());
            }
            
            FileTransformRcPtr t = FileTransform::Create();
            
            const TiXmlAttribute* pAttrib = element->FirstAttribute();
            
            while(pAttrib)
            {
                std::string attrName = pystring::lower(pAttrib->Name());
                
                if(attrName == "src")
                {
                    t->setSrc( pAttrib->Value() );
                }
                else if(attrName == "interpolation")
                {
                    t->setInterpolation( InterpolationFromString(pAttrib->Value()) );
                }
                else if(attrName == "direction")
                {
                    t->setDirection( TransformDirectionFromString(pAttrib->Value()) );
                }
                else
                {
                    // TODO: unknown attr
                }
                pAttrib = pAttrib->Next();
            }
            
            return t;
        }
        
        ConstFileTransformRcPtr GetDefaultFileTransform()
        {
            static ConstFileTransformRcPtr ft = FileTransform::Create();
            return ft;
        }
        
        TiXmlElement * GetElement(const ConstFileTransformRcPtr & t)
        {
            TiXmlElement * element = new TiXmlElement( "file" );
            element->SetAttribute("src", t->getSrc());
            
            ConstFileTransformRcPtr def = GetDefaultFileTransform();
            
            if(t->getInterpolation() != def->getInterpolation())
            {
                const char * interp = InterpolationToString(t->getInterpolation());
                element->SetAttribute("interpolation", interp);
            }
            
            if(t->getDirection() != def->getDirection())
            {
                const char * dir = TransformDirectionToString(t->getDirection());
                element->SetAttribute("direction", dir);
            }
            
            return element;
        }
        
        
        
        
        
        ///////////////////////////////////////////////////////////////////////
        //
        // ColorSpaceTransform
        
        ColorSpaceTransformRcPtr CreateColorSpaceTransform(const TiXmlElement * element)
        {
            if(!element)
                throw Exception("ColorSpaceTransform received null XmlElement.");
            
            if(std::string(element->Value()) != "colorspacetransform")
            {
                std::ostringstream os;
                os << "HandleElement passed incorrect element type '";
                os << element->Value() << "'. ";
                os << "Expected 'colorspacetransform'.";
                throw Exception(os.str().c_str());
            }
            
            ColorSpaceTransformRcPtr t = ColorSpaceTransform::Create();
            
            const char * src = element->Attribute("src");
            const char * dst = element->Attribute("dst");
            
            if(!src || !dst)
            {
                std::ostringstream os;
                os << "ColorSpaceTransform must specify both src and dst ";
                os << "ColorSpaces.";
                throw Exception(os.str().c_str());
            }
            
            t->setSrc(src);
            t->setDst(dst);
            
            const char * direction = element->Attribute("direction");
            if(direction)
            {
                t->setDirection( TransformDirectionFromString(direction) );
            }
            
            return t;
        }
        
        
        
        TiXmlElement * GetElement(const ConstColorSpaceTransformRcPtr & t)
        {
            TiXmlElement * element = new TiXmlElement( "colorspacetransform" );
            
            element->SetAttribute("src", t->getSrc());
            element->SetAttribute("dst", t->getDst());
            
            if(t->getDirection() != TRANSFORM_DIR_FORWARD)
            {
                const char * dir = TransformDirectionToString(t->getDirection());
                element->SetAttribute("direction", dir);
            }
            
            return element;
        }
        
        
        
        
        
        ///////////////////////////////////////////////////////////////////////
        //
        // GroupTransform
        
        GroupTransformRcPtr CreateGroupTransform(const TiXmlElement * element)
        {
            if(!element)
                throw Exception("CreateGroupTransform received null XmlElement.");
            
            if(std::string(element->Value()) != "group")
            {
                std::ostringstream os;
                os << "HandleElement passed incorrect element type '";
                os << element->Value() << "'. ";
                os << "Expected 'group'.";
                throw Exception(os.str().c_str());
            }
            
            GroupTransformRcPtr t = GroupTransform::Create();
            
            // TODO: better error handling; Require Attrs Exist
            
            // Read attributes
            {
                const TiXmlAttribute* pAttrib = element->FirstAttribute();
                while(pAttrib)
                {
                    std::string attrName = pystring::lower(pAttrib->Name());
                    if(attrName == "direction") t->setDirection( TransformDirectionFromString(pAttrib->Value()) );
                    else
                    {
                        // TODO: unknown attr
                    }
                    pAttrib = pAttrib->Next();
                }
            }
            
            // Traverse Children
            const TiXmlElement* pElem = element->FirstChildElement();
            while(pElem)
            {
                std::string elementtype = pElem->Value();
                if(elementtype == "group")
                {
                    t->push_back( CreateGroupTransform(pElem) );
                }
                else if(elementtype == "file")
                {
                    t->push_back( CreateFileTransform(pElem) );
                }
                else if(elementtype == "colorspacetransform")
                {
                    t->push_back( CreateColorSpaceTransform(pElem) );
                }
                else
                {
                    std::ostringstream os;
                    os << "CreateGroupTransform passed unknown element type '";
                    os << elementtype << "'.";
                    throw Exception(os.str().c_str());
                }
                
                pElem = pElem->NextSiblingElement();
            }
            
            return t;
        }
        
        const ConstGroupTransformRcPtr & GetDefaultGroupTransform()
        {
            static ConstGroupTransformRcPtr gt = GroupTransform::Create();
            return gt;
        }
        
        TiXmlElement * GetElement(const ConstGroupTransformRcPtr& t)
        {
            TiXmlElement * element = new TiXmlElement( "group" );
            
            ConstGroupTransformRcPtr def = GetDefaultGroupTransform();
            
            if(t->getDirection() != def->getDirection())
            {
                const char * dir = TransformDirectionToString(t->getDirection());
                element->SetAttribute("direction", dir);
            }
            
            for(int i=0; i<t->size(); ++i)
            {
                ConstTransformRcPtr child = t->getTransform(i);
                
                if(ConstGroupTransformRcPtr groupTransform = \
                    DynamicPtrCast<const GroupTransform>(child))
                {
                    TiXmlElement * childElement = GetElement(groupTransform);
                    element->LinkEndChild( childElement );
                }
                else if(ConstFileTransformRcPtr fileTransform = \
                    DynamicPtrCast<const FileTransform>(child))
                {
                    TiXmlElement * childElement = GetElement(fileTransform);
                    element->LinkEndChild( childElement );
                }
                else if(ConstColorSpaceTransformRcPtr colorSpaceTransform = \
                    DynamicPtrCast<const ColorSpaceTransform>(child))
                {
                    TiXmlElement * childElement = GetElement(colorSpaceTransform);
                    element->LinkEndChild( childElement );
                }
                else
                {
                    throw Exception("Cannot serialize Transform type to XML");
                }
            }
            
            return element;
        }
        
        
        
        ///////////////////////////////////////////////////////////////////////
        //
        // ColorSpace
        
        ColorSpaceRcPtr CreateColorSpaceFromElement(const TiXmlElement * element)
        {
            if(std::string(element->Value()) != "colorspace")
                throw Exception("HandleElement GroupTransform passed incorrect element type.");
            
            ColorSpaceRcPtr cs = ColorSpace::Create();
            
            // TODO: clear colorspace
            // TODO: better error handling; Require Attrs Exist
            
            // Read attributes
            {
                const TiXmlAttribute* pAttrib = element->FirstAttribute();
                double dval = 0.0;
                while(pAttrib)
                {
                    std::string attrName = pystring::lower(pAttrib->Name());
                    if(attrName == "name") cs->setName( pAttrib->Value() );
                    else if(attrName == "family") cs->setFamily( pAttrib->Value() );
                    else if(attrName == "bitdepth") cs->setBitDepth( BitDepthFromString(pAttrib->Value()) );
                    else if(attrName == "isdata") cs->setIsData( BoolFromString(pAttrib->Value()) );
                    else if(attrName == "gpuallocation") cs->setGpuAllocation( GpuAllocationFromString(pAttrib->Value()) );
                    else if(attrName == "gpumin" && pAttrib->QueryDoubleValue(&dval) == TIXML_SUCCESS ) cs->setGpuMin( (float)dval );
                    else if(attrName == "gpumax" && pAttrib->QueryDoubleValue(&dval) == TIXML_SUCCESS ) cs->setGpuMax( (float)dval );
                    else
                    {
                        // TODO: unknown attr
                    }
                    pAttrib = pAttrib->Next();
                }
            }
            
            // Traverse Children
            const TiXmlElement* pElem = element->FirstChildElement();
            while(pElem)
            {
                std::string elementtype = pElem->Value();
                if(elementtype == "description")
                {
                    const char * text = pElem->GetText();
                    if(text) cs->setDescription(text);
                }
                else
                {
                    ColorSpaceDirection dir = ColorSpaceDirectionFromString(elementtype.c_str());
                    
                    if(dir != COLORSPACE_DIR_UNKNOWN)
                    {
                        const TiXmlElement* gchildElem = pElem->FirstChildElement();
                        while(gchildElem)
                        {
                            std::string childelementtype = gchildElem->Value();
                            if(childelementtype == "group")
                            {
                                cs->setTransform(CreateGroupTransform(gchildElem), dir);
                                break;
                            }
                            else
                            {
                                std::ostringstream os;
                                os << "CreateColorSpaceFromElement passed incorrect element type '";
                                os << childelementtype << "'. 'group' expected.";
                                throw Exception(os.str().c_str());
                            }
                            
                            gchildElem=gchildElem->NextSiblingElement();
                        }
                    }
                }
                
                pElem=pElem->NextSiblingElement();
            }
            
            return cs;
        }
        
        
        
        TiXmlElement * GetElement(const ConstColorSpaceRcPtr & cs)
        {
            TiXmlElement * element = new TiXmlElement( "colorspace" );
            element->SetAttribute("name", cs->getName());
            element->SetAttribute("family", cs->getFamily());
            element->SetAttribute("bitdepth", BitDepthToString(cs->getBitDepth()));
            element->SetAttribute("isdata", BoolToString(cs->isData()));
            element->SetAttribute("gpuallocation", GpuAllocationToString(cs->getGpuAllocation()));
            element->SetDoubleAttribute("gpumin", cs->getGpuMin());
            element->SetDoubleAttribute("gpumax", cs->getGpuMax());
            
            const char * description = cs->getDescription();
            if(strlen(description) > 0)
            {
                TiXmlElement * descElement = new TiXmlElement( "description" );
                element->LinkEndChild( descElement );
                
                TiXmlText * textElement = new TiXmlText( description );
                descElement->LinkEndChild( textElement );
            }
            
            ColorSpaceDirection dirs[2] = { COLORSPACE_DIR_TO_REFERENCE,
                                            COLORSPACE_DIR_FROM_REFERENCE };
            for(int i=0; i<2; ++i)
            {
                ConstGroupTransformRcPtr group = cs->getTransform(dirs[i]);
                
                if(cs->isTransformSpecified(dirs[i]) && !group->empty())
                {
                    std::string childType = ColorSpaceDirectionToString(dirs[i]);
                    
                    TiXmlElement * childElement = new TiXmlElement( childType );
                    
                    // In the words of Jack Handey...
                    // "I believe in making the world safe for our children, but
                    // not our children's children, because I don't think
                    // children should be having sex."
                    
                    TiXmlElement * childsChild = GetElement( group );
                    childElement->LinkEndChild( childsChild );
                
                    element->LinkEndChild( childElement );
                }
            }
            
            return element;
        }
    }
    
    
    
    void Config::writeXML(std::ostream& os) const
    {
        TiXmlDocument doc;
        
        TiXmlElement * element = new TiXmlElement( "ocioconfig" );
        doc.LinkEndChild( element );
        
        try
        {
            element->SetAttribute("version", "1");
            element->SetAttribute("resourcepath", getResourcePath());
            
            // Luma coefficients
            {
                float coef[3] = { 0.0f, 0.0f, 0.0f };
                getDefaultLumaCoefs(coef);
                
                element->SetDoubleAttribute("luma_r", coef[0]);
                element->SetDoubleAttribute("luma_g", coef[1]);
                element->SetDoubleAttribute("luma_b", coef[2]);
            }
            
            const char * description = getDescription();
            if(strlen(description) > 0)
            {
                TiXmlElement * descElement = new TiXmlElement( "description" );
                element->LinkEndChild( descElement );
                
                TiXmlText * textElement = new TiXmlText( description );
                descElement->LinkEndChild( textElement );
            }
            
            for(int i = 0; i < getNumRoles(); ++i)
            {
                std::string roleName = getRoleNameByIndex(i);
                std::string roleKey = std::string("role_") + roleName;
                std::string roleValue = getColorSpace(roleName.c_str())->getName();
                element->SetAttribute(roleKey, roleValue);
            }
            
            for(int i=0; i<getNumDisplayDeviceNames(); ++i)
            {
                const char * device = getDisplayDeviceName(i);
                
                int numTransforms = getNumDisplayTransformNames(device);
                for(int j=0; j<numTransforms; ++j)
                {
                    const char * displayTransformName = getDisplayTransformName(device, j);
                    const char * colorSpace = getDisplayColorSpaceName(device, displayTransformName);
                    
                    if(device && displayTransformName && colorSpace)
                    {
                        TiXmlElement * childElement = new TiXmlElement( "display" );
                        childElement->SetAttribute("device", device);
                        childElement->SetAttribute("name", displayTransformName);
                        childElement->SetAttribute("colorspace", colorSpace);
                        element->LinkEndChild( childElement );
                    }
                }
            }
            
            for(int i = 0; i < getNumColorSpaces(); ++i)
            {
                const char * colorSpace = getColorSpaceNameByIndex(i);
                TiXmlElement * childElement = GetElement(getColorSpace(colorSpace));
                element->LinkEndChild( childElement );
            }
            
            TiXmlPrinter printer;
            printer.SetIndent("    ");
            doc.Accept( &printer );
            os << printer.Str();
        }
        catch( const std::exception & e)
        {
            std::ostringstream error;
            error << "Error writing xml. ";
            error << e.what();
            throw Exception(error.str().c_str());
        }
    }
    
    
    ConstConfigRcPtr Config::CreateFromFile(const char * filename)
    {
        ConfigRcPtr config = Config::Create();
        
        // Assuming the config is already empty...
        
        TiXmlDocument doc(filename);
        
        bool loadOkay = doc.LoadFile();
        if(!loadOkay)
        {
            std::ostringstream os;
            os << "Error parsing ocio configuration file, '" << filename;
            os << "'. " << doc.ErrorDesc();
            if(doc.ErrorRow())
            {
                os << " (line " << doc.ErrorRow();
                os << ", col " << doc.ErrorCol() << ")";
            }
            throw Exception(os.str().c_str());
        }
        
        const TiXmlElement* rootElement = doc.RootElement();
        if(!rootElement || std::string(rootElement->Value()) != "ocioconfig")
        {
            std::ostringstream os;
            os << "Error loading '" << filename;
            os << "'. Please confirm file is 'ocioconfig' format.";
            throw Exception(os.str().c_str());
        }
        
        int version = -1;
        
        int lumaSet = 0;
        float lumacoef[3] = { 0.0f, 0.0f, 0.0f };
        
        // Read attributes
        {
            const TiXmlAttribute* pAttrib=rootElement->FirstAttribute();
            while(pAttrib)
            {
                std::string attrName = pystring::lower(pAttrib->Name());
                if(attrName == "version")
                {
                    if (pAttrib->QueryIntValue(&version)!=TIXML_SUCCESS)
                    {
                        std::ostringstream os;
                        os << "Error parsing ocio configuration file, '" << filename;
                        os << "'. Could not parse integer 'version' tag.";
                        throw Exception(os.str().c_str());
                    }
                }
                else if(attrName == "resourcepath")
                {
                    config->setResourcePath(pAttrib->Value());
                }
                else if(pystring::startswith(attrName, "role_"))
                {
                    std::string role = pystring::slice(attrName, (int)std::string("role_").size());
                    config->setRole(role.c_str(), pAttrib->Value());
                }
                else if(pystring::startswith(attrName, "luma_"))
                {
                    std::string channel = pystring::slice(attrName, (int)std::string("luma_").size());
                    
                    int channelindex = -1;
                    if(channel == "r") channelindex = 0;
                    else if(channel == "g") channelindex = 1;
                    else if(channel == "b") channelindex = 2;
                    
                    if(channelindex<0)
                    {
                        std::ostringstream os;
                        os << "Error parsing ocio configuration file, '" << filename;
                        os << "'. Unknown luma channel '" << channel << "'.";
                        throw Exception(os.str().c_str());
                    }
                    
                    double dval;
                    if(pAttrib->QueryDoubleValue(&dval) != TIXML_SUCCESS )
                    {
                        std::ostringstream os;
                        os << "Error parsing ocio configuration file, '" << filename;
                        os << "'. Bad luma value in channel '" << channelindex << "'.";
                        throw Exception(os.str().c_str());
                    }
                    
                    lumacoef[channelindex] = static_cast<float>(dval);
                    ++lumaSet;
                }
                else
                {
                    // TODO: unknown root attr.
                }
                //if (pAttrib->QueryDoubleValue(&dval)==TIXML_SUCCESS) printf( " d=%1.1f", dval);
                
                pAttrib=pAttrib->Next();
            }
        }
        
        if(version == -1)
        {
            std::ostringstream os;
            os << "Config does not specify a version tag. ";
            os << "Please confirm ocio file is of the expect format.";
            throw Exception(os.str().c_str());
        }
        if(version != 1)
        {
            std::ostringstream os;
            os << "Config is format version '" << version << "',";
            os << " but this library only supports version 1.";
            throw Exception(os.str().c_str());
        }
        
        // Traverse children
        const TiXmlElement* pElem = rootElement->FirstChildElement();
        while(pElem)
        {
            std::string elementtype = pElem->Value();
            if(elementtype == "colorspace")
            {
                ColorSpaceRcPtr cs = CreateColorSpaceFromElement( pElem );
                config->addColorSpace( cs );
            }
            else if(elementtype == "description")
            {
                config->setDescription(pElem->GetText());
            }
            else if(elementtype == "display")
            {
                const char * device = pElem->Attribute("device");
                const char * name = pElem->Attribute("name");
                const char * colorspace = pElem->Attribute("colorspace");
                if(!device || !name || !colorspace)
                {
                    std::ostringstream os;
                    os << "Error parsing ocio configuration file, '" << filename;
                    os << "'. Invalid <display> specification.";
                    throw Exception(os.str().c_str());
                }
                
                config->addDisplayDevice(device, name, colorspace);
            }
            else
            {
                std::cerr << "[OCIO WARNING]: Parse error, ";
                std::cerr << "unknown element type '" << elementtype << "'." << std::endl;
            }
            
            pElem=pElem->NextSiblingElement();
        }
        
        config->m_impl->originalFileDir_ = path::dirname(filename);
        config->m_impl->resolvedResourcePath_ = path::join(config->m_impl->originalFileDir_, config->m_impl->resourcePath_);
        
        if(lumaSet != 3)
        {
            std::ostringstream os;
            os << "Error parsing ocio configuration file, '" << filename;
            os << "'. Could not find required ocioconfig luma_{r,g,b} xml attributes.";
            throw Exception(os.str().c_str());
        }
        
        config->setDefaultLumaCoefs(lumacoef);
        
        return config;
    }
}
OCIO_NAMESPACE_EXIT
