#include <iostream>
#include <ismrmrd/dataset.h>

#include "mrd_serialization.hpp"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <MRD H5 FILE>" << std::endl;
    }

    ISMRMRD::Dataset d(argv[1], "dataset", true);

    auto hdr = read_header(std::cin);
    std::stringstream strstream;
    ISMRMRD::serialize(hdr, strstream);
    auto xml_string = strstream.str();
    d.writeHeader(xml_string);

    while (std::cin)
    {
        try
        {
            // read_image returns a variant
            auto im = read_image(std::cin);

            std::visit(
                [&d](auto &&arg)
                {
                    std::stringstream st1;
                    st1 << "image_" << arg.getHead().image_series_index;
                    std::string image_varname = st1.str();

                    // This will pick the right templated appendImage function
                    d.appendImage(image_varname, arg);
                },
                im);
        }
        catch (close_stream &)
        {
            break;
        }
    }
}
