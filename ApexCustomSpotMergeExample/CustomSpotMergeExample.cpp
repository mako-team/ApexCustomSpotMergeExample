/* -----------------------------------------------------------------------
 * <copyright file="CustomSpotMergeExample.cpp" company="Global Graphics Software Ltd">
 *  Copyright (c) 2025 Global Graphics Software Ltd. All rights reserved.
 * </copyright>
 * <summary>
 *  This example is provided on an "as is" basis and without warranty of any kind.
 *  Global Graphics Software Ltd. does not warrant or make any representations
 *  regarding the use or results of use of this example.
 * </summary>
 * -----------------------------------------------------------------------
 */

#include <climits>
#include <vector>
#include <exception>
#include <iostream>
#include <cwctype>
#include <jawsmako/jawsmako.h>
#include <jawsmako/apexcustompostprocess.h>
#include <edl/platform_utils.h>

using namespace JawsMako;
using namespace EDL;

int main()
{
    // These are declared outside the try as it's important that any
    // resources used by the GPU are released *before* the Apex renderer
    // shuts down. Declaring them here ensures that Apex will be released
    // after anything declared inside the try block below.
    IJawsMakoPtr jawsMako;
    IApexRendererPtr apex;
    try
    {
        jawsMako = IJawsMako::create();
        IJawsMako::enableAllFeatures(jawsMako);

        U8String testFilesPath = R"(..\..\TestFiles\)";
        U8String inputFilePath = testFilesPath + "SimpleTest.pdf";
        U8String outputFilePath = testFilesPath + "SimpleTest_p%u.tif";

        // Declare our input pointer
        IInputPtr  input = IInput::create(jawsMako, eFFPDF);

        // We're accessing sequentially, so may as well engage sequential mode
        input->setSequentialMode(true);

        // Open
        IDocumentAssemblyPtr assembly = input->open(inputFilePath);

        // Create the renderer
        apex = IApexRenderer::create(jawsMako);

        // Ok. For this example we'll render to CMYK, but merge in spots "Foo" and "Bar" using a
        // custom post process. To do this merging, we'll upload a pair of simple 256x1 four-channel
        // floating-point images to use as luts for the colour value to use for values of the tints,
        // which will then be merged with the process components using a multiply-like transparency
        // composite. Here we're using two components, but remember that for Apex rendering
        // of spots happens in four-spots-at-a-time chunks.

        // So, create the textures. For fun here, for "Foo" we'll create a mapping that
        // starts white but cycles to green then red as we reach solid. And for "Bar"
        // we'll cycle through Cyan to Yellow. But these could be anything. Note that these
        // computations could just as easily be run on the GPU in the shader, but here we're
        // doing this to demonstrate the use of textures, and in any case, this is contrived.
        // You can attach a number of these textures; the upper limit will vary from GPU to GPU,
        // but should be at least 16 per shader.
        IApexRenderer::ITexturePtr fooLutTexture;
        {
            std::vector<float> fooLutVect;
            fooLutVect.resize(256 * 4);
            for (uint32_t i = 0; i < 256; i++)
            {
                float c = 0.0f;
                float m = 0.0f;
                float y = 0.0f;
                float k = 0.0f;

                float f = static_cast<float>(i);

                // Here we're going to go through green to red. So, cyan will ramp from 0 to 0.5 and
                // then back to 0
                if (i < 128)
                {
                    c = f / 255.0f;
                }
                else
                {
                    c = (255 - f) / 255.0f;
                }
                // Magenta will kick in from 0.5
                if (i >= 128)
                {
                    m = (f - 127) / 127.0f;
                }
                // Yellow will start from 0, reach 1.0, and then maintain 1.0 all the way to 100%
                if (i < 128)
                {
                    y = f / 127.0f;
                }
                else
                {
                    y = 1.0f;
                }

                fooLutVect[i * 4 + 0] = c;
                fooLutVect[i * 4 + 1] = m;
                fooLutVect[i * 4 + 2] = y;
                fooLutVect[i * 4 + 3] = k;
            }
            fooLutTexture = apex->uploadImage(fooLutVect.data(), static_cast<uint32_t>(fooLutVect.size() * sizeof(float)), 4, 256, 1, 32);
        }

        // Now for Bar
        IApexRenderer::ITexturePtr barLutTexture;
        {
            std::vector<float> barLutVect;
            barLutVect.resize(256 * 4);
            for (uint32_t i = 0; i < 256; i++)
            {
                float c = 0.0f;
                float m = 0.0f;
                float y = 0.0f;
                float k = 0.0f;

                float f = static_cast<float>(i);

                // Here we're going to go through cyan to yellow. So, cyan will ramp from 0 to 0.5 and
                // then back to 0
                if (i < 128)
                {
                    c = f / 255.0f;
                }
                else
                {
                    c = (255 - f) / 255.0f;
                }
                // Yellow will start to ramp from halfway
                if (i >= 128)
                {
                    y = (f - 127) / 127.0f;
                }

                barLutVect[i * 4 + 0] = c;
                barLutVect[i * 4 + 1] = m;
                barLutVect[i * 4 + 2] = y;
                barLutVect[i * 4 + 3] = k;
            }
            barLutTexture = apex->uploadImage(barLutVect.data(), static_cast<uint32_t>(barLutVect.size() * sizeof(float)), 4, 256, 1, 32);
        }

        // The shader is on disk here as shader.spv. Load and create.
        IApexRenderer::IFragmentShaderPtr shader;
        {
            IRAInputStreamPtr shaderStream = IInputStream::createFromFile(jawsMako, testFilesPath + "shader.spv");
            shaderStream->openE();
            int64 length = shaderStream->length();
            if (length < 0 || length > INT_MAX)
            {
                throwEDLError(JM_ERR_GENERAL, L"Error getting shader length, or it's too large!");
            }
            CEDLSimpleBuffer shaderBuff(static_cast<uint32_t>(length));
            shaderStream->completeReadE(&shaderBuff[0], static_cast<int32_t>(shaderBuff.size()));
            shaderStream->close();
            shader = apex->createFragmentShader(&shaderBuff[0], static_cast<uint32_t>(shaderBuff.size()));
        }

        // Ok - ready to go.

        // For all documents
        uint32_t ordPageNum = 1;
        for (uint32_t docNum = 0; assembly->documentExists(docNum); docNum++)
        {
            IDocumentPtr document = assembly->getDocument(docNum);
            for (uint32_t pageNum = 0; document->pageExists(pageNum); pageNum++)
            {
                IPagePtr page = document->getPage(pageNum);
                IDOMFixedPagePtr content = page->getContent();
                page->revert();
                page->release();

                // Here we'll just render 8 bit for this example. And we'll use a
                // frame buffer, just because. We'll render at 300dpi for this example.
                const double resolution = 300.0;
                uint32_t width = static_cast<uint32_t>(lround(content->getWidth() / 96.0 * resolution));
                uint32_t height = static_cast<uint32_t>(lround(content->getHeight() / 96.0 * resolution));
                uint32_t stride = width * 4;
                CEDLSimpleBuffer frameBuffer(static_cast<size_t>(stride) * static_cast<size_t>(height));

                // Set up the render spec - basics
                CFrameBufferRenderSpec renderSpec;
                renderSpec.width = width;
                renderSpec.height = height;
                renderSpec.sourceRect = FRect(0.0, 0.0, content->getWidth(), content->getHeight());
                renderSpec.processSpace = IDOMColorSpaceDeviceCMYK::create(jawsMako);
                renderSpec.buffer = &frameBuffer[0];
                renderSpec.rowStride = static_cast<int32_t>(stride);

                // Add our post process to merge the spots
                CShaderParamsVect shaderParams{ CShaderParams(shader, { fooLutTexture, barLutTexture }, CEDLSimpleBuffer()) };
                renderSpec.postProcesses.append(CCustomSpotMergePostProcessSpec::create({ "Foo", "Bar" },
                    shaderParams));

                // Render!
                apex->render(content, &renderSpec);

                // Write to a tiff
                char fileNameBuff[4096];
                // Build the file name - we expect %u in the file path.
                edlSnprintfE(fileNameBuff, sizeof(fileNameBuff), outputFilePath.c_str(), ordPageNum);

                // Create a TIFF encoding frame
                IImageFrameWriterPtr frame;
                (void)IDOMTIFFImage::createWriterAndImage(jawsMako,
                    frame,
                    renderSpec.processSpace,
                    width, height,
                    8,
                    resolution, resolution,
                    IDOMTIFFImage::eTCAuto,
                    IDOMTIFFImage::eTPNone,
                    eIECNone,
                    false,
                    IInputStream::createFromFile(jawsMako, fileNameBuff),
                    IOutputStream::createToFile(jawsMako, fileNameBuff));

                // Out with it
                for (uint32_t y = 0; y < height; y++)
                {
                    frame->writeScanLine(&frameBuffer[y * static_cast<size_t>(stride)]);
                }
                frame->flushData();

                // Done. Onward.
                ordPageNum++;
            }
        }
    }
    catch (IError& e)
    {
        String errorFormatString = getEDLErrorString(e.getErrorCode());
        std::wcerr << L"Exception thrown: " << e.getErrorDescription(errorFormatString) << std::endl;
        // On windows, the return code allows larger numbers, and we can return the error code
        return EXIT_FAILURE;
    }
    catch (std::exception& e)
    {
        std::wcerr << L"std::exception thrown: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}