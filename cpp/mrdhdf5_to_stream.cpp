#include <iostream>
#include <ismrmrd/dataset.h>

#include "mrd_serialization.hpp"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <MRD H5 FILE>" << std::endl;
    }

    // At the moment this will fail is another group is used.
    ISMRMRD::Dataset d(argv[1], "dataset", false);

    std::string xml_head;
    d.readHeader(xml_head);

    // As a validation step, we will deserialize the header
    ISMRMRD::IsmrmrdHeader hdr;
    ISMRMRD::deserialize(xml_head.c_str(), hdr);

    // Write the header to outstream
    write_header(hdr, std::cout);

    // Write out all the acquisitions
    ISMRMRD::Acquisition a;
    for (uint32_t i = 0; i < d.getNumberOfAcquisitions(); i++)
    {
        d.readAcquisition(i, a);
        write_acquisition(a, std::cout);
    }

    // Close it out
    write_message_id(std::cout, MRD_CLOSE);
    std::cout.flush();
}
