/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cmath>

#include <stdio.h>

#include "SipiError.h"
#include "SipiIOPdf.h"
#include "SipiCommon.h"
#include "SipiImage.h"
#include "shttps/Connection.h"
#include "shttps/makeunique.h"

#include "podofo/podofo.h"

#include "poppler/cpp/poppler-document.h"
#include "poppler/cpp/poppler-page.h"
#include "poppler/cpp/poppler-page-renderer.h"
#include "poppler/cpp/poppler-image.h"


static const char __file__[] = __FILE__;
static const int DPI = 300; // TODO: submitted parameter...

namespace Sipi {
    bool SipiIOPdf::read(SipiImage *img, const std::string &filepath, int pagenum, std::shared_ptr<SipiRegion> region,
              std::shared_ptr<SipiSize> size, bool force_bps_8,
              ScalingQuality scaling_quality) {

        //
        // Check for magic number of PDF "%PDF" (hex 25 50 44 46)
        //
        FILE *infile;
        unsigned char header[4];
        if ((infile = fopen(filepath.c_str(), "rb")) == nullptr) {
            return FALSE;
        }
        fread(header, 1, 4, infile);
        fclose(infile);
        if ((header[0] != 0x25) || (header[1] != 0x50) || (header[2] != 0x44) ||(header[3] != 0x46)) {
            return FALSE; // it's not a PDF file
        }


        poppler::document* mypdf = poppler::document::load_from_file(filepath);
        if(mypdf == NULL) {
            std::string msg = "poppler::document::load_from_file failed: " + filepath;
            throw Sipi::SipiImageError(__file__, __LINE__, msg);
        }
        std::cerr << "pdf has " << mypdf->pages() << " pages\n";
        poppler::page* mypage = mypdf->create_page(pagenum - 1);
        poppler::rectf rect = mypage->page_rect();
        std::cerr << "PDF DIMS: " << rect.x() << " " << rect.y() << " " << rect.width() << " " << rect.height() << std::endl;

        poppler::page_renderer renderer;
        renderer.set_render_hint(poppler::page_renderer::text_antialiasing);
        poppler::image myimage = renderer.render_page(mypage, DPI, DPI);
        std::cerr << "created image of  " << myimage.width() << "x"<< myimage.height() << "\n";

        img->nx = myimage.width();
        img->ny = myimage.height();
        int sll = myimage.bytes_per_row();
        img->bps = 8;
        img->icc = std::make_shared<SipiIcc>(icc_sRGB);
        img->photo = RGB;
        std::cerr << "===>" << myimage.format() << std::endl;
        if(myimage.format() == poppler::image::format_rgb24) {
            std::cerr << "----------->rgb24" << std::endl;
            img->nc = 3;
        } else if(myimage.format() == poppler::image::format_argb32) {
            std::cerr << "----------->argb32" << std::endl;
            img->nc = 4;
            img->es.push_back(UNSPECIFIED);
        } else {
            std::string msg = "PDF format invalid: " + filepath;
            throw Sipi::SipiImageError(__file__, __LINE__, msg);
        }
        uint8 *dataptr = new uint8[img->ny * sll];
        memcpy(dataptr, myimage.const_data(), img->ny * sll);
        if (img->nc == 4) {
            for (int y = 0; y < img->ny; y++) {
                for (int x = 0; x < img->nx; x++) {
                    uint8 a = dataptr[y*sll + 4*x];
                    dataptr[y*sll + 4*x] = dataptr[y*sll + 4*x + 2];
                    dataptr[y*sll + 4*x + 2] = a;
                }
            }
        }
        img->pixels = dataptr;
        std::cerr << "Finished conversion of PDF to image..........." << std::endl;

        //
        // Cropping
        //
        if (region != nullptr) { //we just use the image.crop method
            (void) img->crop(region);
        }

        //
        // resize/Scale the image if necessary
        //
        if (size != nullptr) {
            size_t nnx, nny;
            int reduce = -1;
            bool redonly;
            SipiSize::SizeType rtype = size->get_size(img->nx, img->ny, nnx, nny, reduce, redonly);
            if (rtype != SipiSize::FULL) {
                switch (scaling_quality.png) {
                    case HIGH: img->scale(nnx, nny);
                        break;
                    case MEDIUM: img->scaleMedium(nnx, nny);
                        break;
                    case LOW: img->scaleFast(nnx, nny);
                }
            }
        }

        return true;
    };

    SipiImgInfo SipiIOPdf::getDim(const std::string &filepath, int pagenum) {
        SipiImgInfo info;

        //
        // Check for magic number of PDF "%PDF" (hex 25 50 44 46)
        //
        FILE *infile;
        unsigned char header[4];
        if ((infile = fopen(filepath.c_str(), "rb")) == nullptr) {
            info.success = SipiImgInfo::FAILURE;
            return info;        }
        fread(header, 1, 4, infile);
        fclose(infile);
        if ((header[0] != 0x25) || (header[1] != 0x50) || (header[2] != 0x44) ||(header[3] != 0x46)) {
            info.success = SipiImgInfo::FAILURE;
            return info;
        }

        poppler::document* mypdf = poppler::document::load_from_file(filepath);
        if(mypdf == NULL) {
            std::string msg = "poppler::document::load_from_file failed: " + filepath;
            throw Sipi::SipiImageError(__file__, __LINE__, msg);
        }
        info.numpages = mypdf->pages();
        std::cerr << "pdf has " << mypdf->pages() << " pages\n";
        poppler::page* mypage = mypdf->create_page(pagenum < 1 ? 0 : pagenum - 1);
        poppler::rectf rect = mypage->page_rect();
        std::cerr << "PDF DIMS: " << rect.x() << " " << rect.y() << " " << rect.width() << " " << rect.height() << std::endl;
        info.width = lround(rect.width()*DPI/72.);
        info.height = lround(rect.height()*DPI/72.);

        info.success = SipiImgInfo::DIMS;

        return info;
    };

    void SipiIOPdf::write(SipiImage *img, const std::string &filepath, const SipiCompressionParams *params) {
        if (img->bps == 16) img->to8bps();

        //
        // we have to check if the image has an alpha channel (not supported by PDF as we use it). If
        // so, we remove it!
        //
        if ((img->getNc() > 3) && (img->getNalpha() > 0)) { // we have an alpha channel....
            for (size_t i = 3; i < (img->getNalpha() + 3); i++) img->removeChan(i);
        }

        double dScaleX = 1.0;
        double dScaleY = 1.0;
        double dScale  = 1.0;

        PoDoFo::PdfRefCountedBuffer *pdf_buffer = new PoDoFo::PdfRefCountedBuffer(32*1024);
        PoDoFo::PdfOutputDevice *pdf_dev = new PoDoFo::PdfOutputDevice(pdf_buffer);
        PoDoFo::PdfStreamedDocument document(pdf_dev);

        PoDoFo::PdfRect size = PoDoFo::PdfRect(0.0, 0.0, img->nx, img->ny);

        PoDoFo::PdfPainter painter;

        PoDoFo::PdfPage* pPage;
        PoDoFo::PdfImage image( &document );
        switch (img->photo) {
            case MINISWHITE:
            case MINISBLACK: {
                if (img->nc != 1) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 1 (nc = " + std::to_string(img->nc) + ")!");
                }
                image.SetImageColorSpace(PoDoFo::ePdfColorSpace_DeviceGray);
                break;
            }
            case RGB: {
                if (img->nc != 3) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
                }
                image.SetImageColorSpace(PoDoFo::ePdfColorSpace_DeviceRGB);
                break;
            }
            case SEPARATED: {
                if (img->nc != 4) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 4 (nc = " + std::to_string(img->nc) + ")!");
                }
                image.SetImageColorSpace(PoDoFo::ePdfColorSpace_DeviceCMYK);
                break;
            }
            case YCBCR: {
                if (img->nc != 3) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
                }
                throw SipiImageError(__file__, __LINE__, "Unsupported PDF colorspace: " + std::to_string(img->photo));
               break;
            }
            case CIELAB: {
                if (img->nc != 3) {
                    throw SipiImageError(__file__, __LINE__,
                                         "Num of components not 3 (nc = " + std::to_string(img->nc) + ")!");
                }
                image.SetImageColorSpace(PoDoFo::ePdfColorSpace_CieLab);
                break;
            }
            default: {
                throw SipiImageError(__file__, __LINE__, "Unsupported PDF colorspace: " + std::to_string(img->photo));
            }
        }
        PoDoFo::PdfMemoryInputStream inimgstream((const char *) img->pixels, (PoDoFo::pdf_long) img->nx * img->ny * img->nc);
        image.SetImageData	(img->nx, img->ny, 8, &inimgstream);

        pPage = document.CreatePage(size);

        dScaleX = size.GetWidth() / image.GetWidth();
        dScaleY = size.GetHeight() / image.GetHeight();
        dScale  = PoDoFo::PDF_MIN( dScaleX, dScaleY );

        painter.SetPage( pPage );

        if( dScale < 1.0 )
        {
            painter.DrawImage( 0.0, 0.0, &image, dScale, dScale );
        }
        else
        {
            // Center Image
            double dX = (size.GetWidth() - image.GetWidth())/2.0;
            double dY = (size.GetHeight() - image.GetHeight())/2.0;
            painter.DrawImage( dX, dY, &image );
        }
        painter.FinishPage();

        document.GetInfo()->SetCreator (PoDoFo::PdfString("Simple Image Presentation Interface (SIPI)"));
        document.GetInfo()->SetAuthor  (PoDoFo::PdfString("Lukas Rosenthaler"));
        document.GetInfo()->SetTitle   (PoDoFo::PdfString(img->emdata.origname()));
        document.GetInfo()->SetSubject (PoDoFo::PdfString("Unknown") );
        document.GetInfo()->SetKeywords(PoDoFo::PdfString("unknown") );
        document.Close();

        if (filepath == "HTTP") { // we are transmitting the data through the webserver
            shttps::Connection *conn = img->connection();
            conn->sendAndFlush(pdf_buffer->GetBuffer(), pdf_buffer->GetSize());
        }
        else if (filepath == "stdout:") {
            std::cout.write(pdf_buffer->GetBuffer(), pdf_buffer->GetSize());
            std::cout.flush();
        }
        else {
            std::ofstream outfile(filepath, std::ofstream::binary);
            outfile.write (pdf_buffer->GetBuffer(), pdf_buffer->GetSize());
            outfile.close();
        }
        delete pdf_dev;
        delete pdf_buffer;
    };

}
