/*********************************************************************************
 *
 * Inviwo - Interactive Visualization Workshop
 *
 * Copyright (c) 2021 Inviwo Foundation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *********************************************************************************/

#include <modules/base/processors/customimagestackvolumeprocessor-single.h>

#include <inviwo/core/common/inviwoapplication.h>
#include <inviwo/core/datastructures/image/layer.h>
#include <inviwo/core/datastructures/image/layerram.h>
#include <inviwo/core/datastructures/image/layerramprecision.h>
#include <inviwo/core/datastructures/image/imageram.h>
#include <inviwo/core/datastructures/volume/volume.h>
#include <inviwo/core/datastructures/volume/volumeram.h>
#include <inviwo/core/datastructures/volume/volumeramprecision.h>
#include <inviwo/core/io/datareaderfactory.h>
#include <inviwo/core/util/filesystem.h>
#include <inviwo/core/util/stdextensions.h>
#include <inviwo/core/util/vectoroperations.h>
#include <inviwo/core/util/zip.h>
#include <inviwo/core/util/raiiutils.h>
#include <inviwo/core/io/datareaderexception.h>

#include <algorithm>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <openslide/openslide.h>
#include<fstream>

// #define IMG_PATH "/home/sassluck/Desktop/Histopathology/level2.jpg"
// #define OUT_PATH "/home/sassluck/Desktop/Histopathology/output.txt"
namespace inviwo {

namespace {

template <typename Format>
struct FloatOrIntMax32
    : std::integral_constant<bool, Format::numtype == NumericType::Float || Format::compsize <= 4> {
};

}  // namespace


// The Class Identifier has to be globally unique. Use a reverse DNS naming scheme
const ProcessorInfo CustomImageStackVolumeProcessorSingle::processorInfo_{
    "org.inviwo.CustomImageStackVolumeProcessorSingle",      // Class identifier
    "CustomImageStackVolumeProcessorSingle",                // Display name
    "Undefined",              // Category
    CodeState::Experimental,  // Code state
    Tags::None,               // Tags
};
const ProcessorInfo CustomImageStackVolumeProcessorSingle::getProcessorInfo() const { return processorInfo_; }

CustomImageStackVolumeProcessorSingle::CustomImageStackVolumeProcessorSingle(InviwoApplication* app)
    : Processor()
    , inport_("RegionCoordinates")
    , outport_("volume")
    , filePattern_("filePattern", "File Pattern", "####.jpeg", "")
    , reload_("reload", "Reload data")
    , skipUnsupportedFiles_("skipUnsupportedFiles", "Skip Unsupported Files", false)
    , isRectanglePresent_("isRectanglePresent", "Is Rectangle Present", false)
    , basis_("Basis", "Basis and offset")
    , information_("Information", "Data information")
    , readerFactory_{app->getDataReaderFactory()}
    , level_("level", "Level", 2, 0, 2) //The maximum level in the test slide is 3. Need to generalise it later.
    // , coordinates_("Coordinates", "Coordinates", size2_t(0),
    //                size2_t(std::numeric_limits<size_t>::lowest()),
    //                size2_t(std::numeric_limits<size_t>::max()), size2_t(1),
    //                InvalidationLevel::Valid, PropertySemantics::Text)
    , zoom_in(2)
    , coordinateX_("PointerX","PointerX",0,0,10000,100)
    , coordinateY_("PointerY","PointerY",0,0,10000,100)
    , modified(0)
    // level 2 1000,1000
    // level 1 1000,1000
    {
    addPort(inport_);
    addPort(outport_);
    addProperty(filePattern_);
    addProperty(reload_);
    addProperty(skipUnsupportedFiles_);
    addProperty(isRectanglePresent_);
    isRectanglePresent_.onChange([this](){process();});

    addProperty(basis_);
    addProperty(information_);
    isSink_.setUpdate([]() { return true; });
    isReady_.setUpdate([this]() { return !filePattern_.getFileList().empty(); });
    filePattern_.onChange([&]() {isReady_.update();});

    addFileNameFilters();

    addProperty(level_);
    // addProperty(coordinates_);
    addProperty(coordinateX_);
    addProperty(coordinateY_);

    level_.onChange([&](){process();});
    // coordinates_.onChange([&](){process();});
    coordinateX_.onChange([&](){process();});
    coordinateY_.onChange([&](){process();});

    inport_.onChange([&]() {
        isReady_.update(); 
        process();
        });
}

std::ofstream myFile;
void CustomImageStackVolumeProcessorSingle::ImgOutput(unsigned char byte)
{
  myFile << byte;
}
void CustomImageStackVolumeProcessorSingle::SlideExtractor(std::string PATH){
    // std::cout << "Image path is : "<< PATH << std::endl;
    openslide_t* op = openslide_open(PATH.c_str());
    int32_t level = level_.get();
    int64_t dim_lvlk[2];
    int64_t dim_lvl0[2];
    level_.setMaxValue(openslide_get_level_count(op)-1);

    if(op!=0)
    {
        openslide_get_level_dimensions(op,0,&dim_lvl0[0],&dim_lvl0[1]);
        openslide_get_level_dimensions(op,level,&dim_lvlk[0],&dim_lvlk[1]);
    }
    else{
        std::cout << "Please enter a valid image path" << std::endl;
        return;
    }
    /*
    Rectangle based calculation 
    */
    coordinateX_.setMaxValue(dim_lvlk[0]);
    coordinateY_.setMaxValue(dim_lvlk[1]);
    // coordinateX_.set(coordinateX_.get(),0,dim_lvlk[0],100);
    // coordinateY_.set(coordinateY_.get(),0,dim_lvlk[1],100);
    _Float64x start_x,start_y;
    int64_t w,h;
    if(!inport_.hasData())
    {
        start_x=coordinateX_.get(),start_y=coordinateY_.get(),w=1920,h=1080; 
    }
    else
    {
        start_x = inport_.getData()->at(0).x;
        start_y = inport_.getData()->at(0).y;

        w = abs(inport_.getData()->at(0).x - inport_.getData()->at(1).x);
        h = abs(inport_.getData()->at(0).y - inport_.getData()->at(2).y);

        w *= (dim_lvl0[0]/dim_lvlk[0]);
        h *= (dim_lvl0[1]/dim_lvlk[1]);
        if(w > 1920) w=1920;
        if(h > 1080) w=1080;
    }
    if(level!=zoom_in){
        std::cout << "Zoomed to level: " << level << std::endl;
        std::cout << "Previous coordinates: " << start_x << " " << start_y << std::endl;
        int64_t dim_lvl_prev[2];
        openslide_get_level_dimensions(op,zoom_in,&dim_lvl_prev[0],&dim_lvl_prev[1]);
        start_x*=(dim_lvlk[0]/(float)dim_lvl_prev[0]);
        start_y*=(dim_lvlk[1]/(float)dim_lvl_prev[1]);
        std::cout << "New coordinates: " << start_x << " " << start_y << std::endl;
        zoom_in = level;
        coordinateX_.set(start_x);
        coordinateY_.set(start_y);
        // std::cout << "Zoom value: " << zoom_in << std::endl;
    }
    if(start_x + w >= dim_lvlk[0]){
        start_x = dim_lvlk[0]-w;
    }
    if(start_y + h >= dim_lvlk[1]){
        start_y = dim_lvlk[1]-h;
    }
    start_x*=(dim_lvl0[0]/(float)dim_lvlk[0]);
    start_y*=(dim_lvl0[1]/(float)dim_lvlk[1]);

    std::cout << "Coordinates and dimensions in level 0: " << start_x << " " << start_y << " " << w << " " << h << std::endl;

    int64_t size_img=w*h*4;
    uint32_t *dest = (uint32_t*)malloc(size_img);

    if(op!=0){
        openslide_read_region(op,dest,floor(start_x),floor(start_y),level,w,h);
        openslide_close(op);
        std::cout << "in OP" << std::endl;
    }

    // std::cout<< "(" << dim_lvlk[0] << "," << dim_lvlk[1] << ")" << std::endl;

    int bytesperpixel=3;
    auto pixels = new unsigned char[w*h*bytesperpixel];

    for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
        uint32_t argb = dest[y * w + x];
        // double alpha = (argb >> 24)& 0xff;
        double red = (argb >> 16)& 0xff;
        double green = (argb >> 8)& 0xff;
        double blue = (argb >> 0)& 0xff;
        // std::cout << red << " " << green << " " << blue << std::endl;
        int offset = (y * w + x)*bytesperpixel;
        pixels[offset]=red;
        pixels[offset+1]=green;
        pixels[offset+2]=blue;
        // std::cout << alpha;
    //   std::cout << ((argb >> 16)& 0xff) << " ";
    }}
    // free(dest);
    delete []pixels;
    TooJpeg::writeJpeg(ImgOutput, pixels, w, h);
    std::ofstream out(CustomImageStackVolumeProcessorSingle::IMAGES_LOC);
    std::string img_name = CustomImageStackVolumeProcessorSingle::IMG_PATH + "\n";
    for(int i=0;i<50;i++)
        out << img_name;
    out.close();
    // std::cout << "Openslide is working" << std::endl;
}

void CustomImageStackVolumeProcessorSingle::addFileNameFilters() {
    filePattern_.clearNameFilters();
    filePattern_.addNameFilter(FileExtension::all());
    filePattern_.addNameFilters(readerFactory_->getExtensionsForType<Layer>());
}

void CustomImageStackVolumeProcessorSingle::process() {
    // outport_.setData(myImage);
    // my_slide();
    if(modified == 1)
        return;
    util::OnScopeExit guard{[&]() { outport_.setData(nullptr); }};
    myFile.open(CustomImageStackVolumeProcessorSingle::IMG_PATH, std::ios_base::out | std::ios_base::binary);
    myFile.close();
    if (filePattern_.isModified() || reload_.isModified() || skipUnsupportedFiles_.isModified() || isRectanglePresent_.isModified() || level_.isModified() || coordinateX_.isModified() || coordinateY_.isModified()) {
        myFile.open(CustomImageStackVolumeProcessorSingle::IMG_PATH, std::ios_base::out | std::ios_base::binary);
        auto image_paths = filePattern_.getFileList();
        std::cout << "Current image path is : " << image_paths[0] << std::endl;
        modified=1;
        SlideExtractor(image_paths[0]);
        modified=0;
        volume_ = load();
        if (volume_) {
            basis_.updateForNewEntity(*volume_, deserialized_);
            information_.updateForNewVolume(*volume_, deserialized_);
        }
        deserialized_ = false;
        // myFile.close();
    }

    if (volume_) {
        basis_.updateEntity(*volume_);
        information_.updateVolume(*volume_);
    }
    outport_.setData(volume_);
    guard.release();
}

bool CustomImageStackVolumeProcessorSingle::isValidImageFile(std::string fileName) {
    return readerFactory_->hasReaderForTypeAndExtension<Layer>(fileName);
}

std::shared_ptr<Volume> CustomImageStackVolumeProcessorSingle::load() {
    std::ifstream file1(CustomImageStackVolumeProcessorSingle::IMAGES_LOC);
    std::string img;
    std::vector<std::string> img_names;
    while(getline(file1,img)){
        img_names.push_back(img);
    }
    const auto files = img_names;
    for(uint64_t i=0;i<files.size();i++)
        std::cout << files[i] << std::endl;
    // std::cout << "This is my file size : " << files.size() << endl;
    // std::cout << files
    if (files.empty()) {
        return nullptr;
    }

    std::vector<std::pair<std::string, std::unique_ptr<DataReaderType<Layer>>>> slices;
    slices.reserve(files.size());

    std::transform(
        files.begin(), files.end(), std::back_inserter(slices),
        [&](const auto& file) -> std::pair<std::string, std::unique_ptr<DataReaderType<Layer>>> {
            return {file, std::move(readerFactory_->getReaderForTypeAndExtension<Layer>(
                              filePattern_.getSelectedExtension(), file))};
        });
    if (skipUnsupportedFiles_) {
        slices.erase(std::remove_if(slices.begin(), slices.end(),
                                    [](auto& elem) { return elem.second == nullptr; }),
                     slices.end());
    }

    // identify first slice with a reader
    const auto first = std::find_if(slices.begin(), slices.end(),
                                    [](auto& item) { return item.second != nullptr; });
    if (first == slices.end()) {  // could not find any suitable data reader for the images
        throw Exception(
            fmt::format("No supported images found in '{}'", filePattern_.getFilePatternPath()),
            IVW_CONTEXT);
    }

    const auto referenceLayer = first->second->readData(first->first);
    // std::cout << " This is the first layer : " << referenceLayer ;
    // Call getRepresentation here to enforce creating a ram representation.
    // Otherwise the default image size, i.e. 256x256, will be reported since the LayerDisk
    // does not provide meta data for all image formats.
    const auto referenceRAM = referenceLayer->getRepresentation<LayerRAM>();
    if (glm::compMul(referenceRAM->getDimensions()) == 0) {
        throw Exception(
            fmt::format("Could not extract valid image dimensions from '{}'", first->first),
            IVW_CONTEXT);
    }

    const auto refFormat = referenceRAM->getDataFormat();
    if ((refFormat->getNumericType() != NumericType::Float) && (refFormat->getPrecision() > 32)) {
        throw DataReaderException(
            fmt::format("Unsupported integer bit depth ({})", refFormat->getPrecision()),
            IVW_CONTEXT);
    }

    return referenceRAM->dispatch<std::shared_ptr<Volume>, FloatOrIntMax32>(
        [&](auto reflayerprecision) {
            using ValueType = util::PrecisionValueType<decltype(reflayerprecision)>;
            using PrimitiveType = typename DataFormat<ValueType>::primitive;

            const size2_t layerDims = reflayerprecision->getDimensions();
            const size_t sliceOffset = glm::compMul(layerDims);

            // create matching volume representation
            auto volumeRAM =
                std::make_shared<VolumeRAMPrecision<ValueType>>(size3_t{layerDims, slices.size()});
            auto volData = volumeRAM->getDataTyped();

            const auto fill = [&](size_t s) {
                std::fill(volData + s * sliceOffset, volData + (s + 1) * sliceOffset, ValueType{0});
            };

            const auto read = [&](const auto& file, auto reader) -> std::shared_ptr<Layer> {
                try {
                    return reader->readData(file);
                } catch (DataReaderException const& e) {
                    LogProcessorWarn(
                        fmt::format("Could not load image: {}, {}", file, e.getMessage()));
                    return nullptr;
                }
            };

            for (auto&& elem : util::enumerate(slices)) {
                const auto slice = elem.first();
                const auto file = elem.second().first;
                const auto reader = elem.second().second.get();
                if (!reader) {
                    fill(slice);
                    continue;
                }

                const auto layer = read(file, reader);
                if (!layer) {
                    fill(slice);
                    continue;
                }
                const auto layerRAM = layer->template getRepresentation<LayerRAM>();

                const auto format = layerRAM->getDataFormat();
                if ((format->getNumericType() != NumericType::Float) &&
                    (format->getPrecision() > 32)) {
                    LogProcessorWarn(fmt::format("Unsupported integer bit depth: {}, for image: {}",
                                                 format->getPrecision(), file));
                    fill(slice);
                    continue;
                }

                if (layerRAM->getDimensions() != layerDims) {
                    LogProcessorWarn(
                        fmt::format("Unexpected dimensions: {} , expected: {}, for image: {}",
                                    layer->getDimensions(), layerDims, file));
                    fill(slice);
                    continue;
                }
                layerRAM->template dispatch<void, FloatOrIntMax32>([&](auto layerpr) {
                    const auto data = layerpr->getDataTyped();
                    std::transform(
                        data, data + sliceOffset, volData + slice * sliceOffset,
                        [](auto value) { return util::glm_convert_normalized<ValueType>(value); });
                });
            }

            auto volume = std::make_shared<Volume>(volumeRAM);
            volume->dataMap_.dataRange =
                dvec2{DataFormat<PrimitiveType>::lowest(), DataFormat<PrimitiveType>::max()};
            volume->dataMap_.valueRange =
                dvec2{DataFormat<PrimitiveType>::lowest(), DataFormat<PrimitiveType>::max()};

            const auto size = vec3(0.01f) * static_cast<vec3>(volumeRAM->getDimensions());
            volume->setBasis(glm::diagonal3x3(size));
            volume->setOffset(-0.5 * size);

            return volume;
        });
}

void CustomImageStackVolumeProcessorSingle::deserialize(Deserializer& d) {
    Processor::deserialize(d);
    addFileNameFilters();
    deserialized_ = true;
}

}  // namespace inviwo