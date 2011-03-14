#include "oggparser.hpp"
#include <cstring>
#include <cassert>
#include <malloc.h>

typedef std::list<oggparser::OggPage> pages_t;

long oggparser::ReadInt(
    IOggReader* pReader,
    long long pos,
    long len,
    long long& val)
{
    val = 0;

    for (long i = 0; i < len; ++i)
    {
        unsigned char b;

        const long result = pReader->Read(pos, 1, &b);

        if (result < 0)  //error
            return len;

        const long long bb = static_cast<long long>(b) << (i * 8);

        val |= bb;
    }

    return 0;  //success
}


namespace oggparser
{

IOggReader::~IOggReader()
{
}


long OggPage::Read(IOggReader* pReader, long long& pos)
{
    if (pos < 0)
        return -1;

    long result = pReader->Read(pos, 4, m_capture_pattern);

    if (result < 0)  //error
        return result;

    if (memcmp(m_capture_pattern, "OggS", 4) != 0)
        return E_FILE_FORMAT_INVALID;

    pos += 4;  //consume capture_pattern

    long long version;

    result = ReadInt(pReader, pos, 1, version);

    if (result < 0)  //error
        return result;

    ++pos;  //consume version

    m_version = static_cast<unsigned char>(version);

    long long header;

    result = ReadInt(pReader, pos, 1, header);

    if (result < 0)  //error
        return result;

    ++pos;  //consume header flag

    m_header = static_cast<unsigned char>(header);

    result = ReadInt(pReader, pos, 8, m_granule_pos);

    if (result < 0)  //error
        return result;

    pos += 8;  //consume granule pos

    long long serial_num;

    result = ReadInt(pReader, pos, 4, serial_num);

    if (result < 0)  //error
        return result;

    pos += 4;  //consume serial number

    m_serial_num = static_cast<unsigned long>(serial_num);

    long long sequence_num;

    result = ReadInt(pReader, pos, 4, sequence_num);

    if (result < 0)  //error
        return result;

    pos += 4;  //consume page sequence number

    m_sequence_num = static_cast<unsigned long>(sequence_num);

    long long crc;

    result = ReadInt(pReader, pos, 4, crc);

    if (result < 0)  //error
        return result;

    pos += 4;  //consume crc

    //http://www.ross.net/crc/download/crc_v3.txt

    m_crc = static_cast<unsigned long>(crc);

    long long segments_count;

    result = ReadInt(pReader, pos, 1, segments_count);

    if (result < 0)  //error
        return result;

    if (segments_count <= 0)   //TODO: confirm this
        return E_FILE_FORMAT_INVALID;

    ++pos;  //consume segment count

    m_descriptors.clear();

    while (segments_count > 0)
    {
        m_descriptors.push_back(Descriptor());

        {
            Descriptor& data = m_descriptors.back();

            data.pos = -1;  //fill in later
            data.len = 0;
        }

        for (;;)
        {
            long long lacing_value;

            result = ReadInt(pReader, pos, 1, lacing_value);

            if (result < 0)  //error
                return result;

            ++pos;  //consume lacing value

            {
                Descriptor& payload = m_descriptors.back();
                payload.len += static_cast<long>(lacing_value);

                if (--segments_count <= 0)
                {
                    if (lacing_value == 255)  //pkt continued on next page
                        payload.len = -payload.len;
                    else  //pkt completed on curr page
                        m_header |= OggPage::fDone;

                    break;
                }
            }

            if (lacing_value != 255)
                break;
        }
    }

    typedef descriptors_t::iterator iter_t;

    iter_t iter = m_descriptors.begin();
    const iter_t iter_end = m_descriptors.end();

    while (iter != iter_end)
    {
        Descriptor& payload = *iter++;

        payload.pos = pos;
        pos += labs(payload.len);
    }

    return 0;
}


long OggStream::Create(IOggReader* pReader, OggStream*& pStream)
{
    if (pReader == NULL)
        return -1;

    //the stream of bytes we have is the "physical" bitstream.
    //there are the bytes stored in the container file.

    //the physical bitstream encapsulates one or more "logical"
    //bitstreams, provided by the encoder.  i assume this is
    //the same as a matroska track or an avi stream.

    //a logical bitream contains "packets", which is the same
    //thing as a frame.

    //a physical bitstream comprises "pages".  does a single page
    //contain only single logical bitstream or can it include
    //multiple logical bistreams???  [one logical bitstream only]

    //a page has a unique serial number that identifies the
    //logical bitstream.  this is analogous to a matroska track number
    //or an avi stream identifier

    //a logical bitstream has a special(?) start page (bos) and
    //another special(?) stop page (eos)

    //the bos identifies the codec type, for audio it contains
    //sampling rate, etc

    //bos uses a magic number to uniquely identify the codec type

    //the format of a bos is specific to each codec type

    //ogg allows "secondary header packets" to follow bos but
    //precede data packets; these are put on an integral number
    //of pages, which do not also have data packets

    //"Ogg Vorbis" is a "media mapping" that describes how the
    //logical bitstream for vorbis audio is encapsulated into
    //an ogg physical bitstream.
    //bos
    //  name of vorbis codec
    //  revision of vorbis codec
    //  audio rate
    //  audio quality
    //two more header pages
    //
    //bos: 1st 7 bytes are:
    //  0x01
    //  "vorbis"

    //data for a logical bitstream comes in order and has
    //position markers called "Granule positions".  This is an
    //alternative to using "time" directly: position markers are
    //unitless and increase sequentially.  To get a time you
    //must convert the granule position.

    //Ogg pages have a max size of 64kB (65307); this means that a large
    //packet ("frame") will need to be distributed over one or more
    //pages.

    //a packet is itself divided into 255-byte chunks called "segments",
    //plus one more segment with fewer than 255 bytes

    //the segments (which have no header boundaries themselves) are
    //grouped into a page preceded by a header.  The page header
    //contains a "segment table" gives info about the sizes ("lacing")
    //of the segments.

    //a flag in page hdr tells whether this page contains a packet
    //continued from a previous page.

    //from rfc5334.txt:
    //"In particular, .ogg is used for Ogg files that
    //contain only a Vorbis bitstream,"

    //Vorbis_I_spec.html
    //
    //A.2 Encapsulation

    //Ogg encapsulation of a Vorbis packet stream is straightforward.

    //The first Vorbis packet (the identification header),
    //which uniquely identifies a stream as Vorbis audio,
    //is placed alone in the first page of the logical Ogg stream.
    //This results in a first Ogg page of exactly 58 bytes at the
    //very beginning of the logical stream.

    //This first page is marked �beginning of stream� in the page flags.

    //The second and third vorbis packets (comment and setup headers)
    //may span one or more pages beginning on the second page of the
    //logical stream. However many pages they span, the third header
    //packet finishes the page on which it ends. The next (first audio)
    //packet must begin on a fresh page.

    //The granule position of these first pages containing only
    //headers is zero.

    //The first audio packet of the logical stream begins a fresh Ogg page.

    //Packets are placed into ogg pages in order until the end of stream.

    //The last page is marked �end of stream� in the page flags.

    //Vorbis packets may span page boundaries.

    //The granule position of pages containing Vorbis audio is in units of
    //PCM audio samples (per channel; a stereo stream�s granule position does
    //not increment at twice the speed of a mono stream).

    //The granule position of a page represents the end PCM sample position
    //of the last packet completed on that page. The �last PCM sample� is
    //the last complete sample returned by decode, not an internal sample
    //awaiting lapping with a subsequent block. A page that is entirely
    //spanned by a single packet (that completes on a subsequent page)
    //has no granule position, and the granule position is set to �-1�.
    //
    //Note that the last decoded (fully lapped) PCM sample from a packet
    //is not necessarily the middle sample from that block. If, eg,
    //the current Vorbis packet encodes a �long block� and the next
    //Vorbis packet encodes a �short block�, the last decodable sample
    //from the current packet be at position
    //  (3*long_block_length/4) - (short_block_length/4).

    //The granule (PCM) position of the first page need not indicate that
    //the stream started at position zero. Although the granule position
    //belongs to the last completed packet on the page and a valid
    //granule position must be positive, by inference it may indicate
    //that the PCM position of the beginning of audio is positive or negative.
    //
    //A positive starting value simply indicates that this stream begins
    //at some positive time offset, potentially within a larger program.
    //This is a common case when connecting to the middle of broadcast stream.
    //
    //A negative value indicates that output samples preceeding time zero
    //should be discarded during decoding; this technique is used to allow
    //sample-granularity editing of the stream start time of already-encoded
    //Vorbis streams. The number of samples to be discarded must not exceed
    //the overlap-add span of the first two audio packets.
    //
    //In both of these cases in which the initial audio PCM starting
    //offset is nonzero, the second finished audio packet must flush
    //the page on which it appears and the third packet begin a fresh page.
    //This allows the decoder to always be able to perform PCM position
    //adjustments before needing to return any PCM data from synthesis,
    //resulting in correct positioning information without any aditional
    //seeking logic.
    //
    //Note: Failure to do so should, at worst, cause a decoder implementation
    //to return incorrect positioning information for seeking operations at
    //the very beginning of the stream.

    //A granule position on the final page in a stream that indicates
    //less audio data than the final packet would normally return is
    //used to end the stream on other than even frame boundaries.
    //The difference between the actual available data returned and
    //the declared amount indicates how many trailing samples to
    //discard from the decoding process.

    //rfc3533.txt:
    //
    //6. The Ogg page format

    //   A physical Ogg bitstream consists of a sequence of concatenated
    //   pages.  Pages are of variable size, usually 4-8 kB, maximum 65307
    //   bytes.  A page header contains all the information needed to
    //   demultiplex the logical bitstreams out of the physical bitstream and
    //   to perform basic error recovery and landmarks for seeking.  Each page
    //   is a self-contained entity such that the page decode mechanism can
    //   recognize, verify, and handle single pages at a time without
    //   requiring the overall bitstream.

    //   The Ogg page header has the following format:

    // 0                   1                   2                   3
    // 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1| Byte
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //| capture_pattern: Magic number for page start "OggS"           | 0-3
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //| version       | header_type   | granule_position              | 4-7
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //|                                                               | 8-11
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //|                               | bitstream_serial_number       | 12-15
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //|                               | page_sequence_number          | 16-19
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //|                               | CRC_checksum                  | 20-23
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //|                               |page_segments  | segment_table | 24-27
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //| ...                                                           | 28-
    //+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

    //   The LSb (least significant bit) comes first in the Bytes.  Fields
    //   with more than one byte length are encoded LSB (least significant
    //   byte) first.


    pStream = new OggStream(pReader);

    //Vorbis_I_spec.html
    //Section A.2

    //Ogg page
    //http://en.wikipedia.org/wiki/Ogg_page

    const long result = pStream->Init();

    if (result >= 0)
        return 0;  //must capture ident, comment, and setup pkts

    delete pStream;
    pStream = NULL;

    return result;
}


OggStream::OggStream(IOggReader* p) :
    m_pReader(p),
    m_pos(0)
{
}


OggStream::~OggStream()
{
}


long OggStream::Init()
{
    OggPage page;

    long result = ReadPage(page);

    if (result < 0)
        return result;

    if (!(page.m_header & OggPage::fBOS))
        return E_FILE_FORMAT_INVALID;

    if (page.m_header & OggPage::fEOS)
        return E_FILE_FORMAT_INVALID;

    if (!(page.m_header & OggPage::fDone))
        return E_FILE_FORMAT_INVALID;

    if (page.m_granule_pos != 0)
        return E_FILE_FORMAT_INVALID;

    if (m_pos != 58)
        return E_FILE_FORMAT_INVALID;

    Packet ident;

    if (GetPacket(ident) < 1)
        return E_FILE_FORMAT_INVALID;

    //TODO: must vet packet itself

    {
        const char str[] = "\x01vorbis";

        const OggPage::descriptors_t& dd = ident.descriptors;

        const OggPage::Descriptor& d = dd.front();
        assert(d.len > 0);
        assert(size_t(d.len) >= strlen(str));

        void* const p = _malloca(d.len);
        unsigned char* const buf = static_cast<unsigned char*>(p);

        result = m_pReader->Read(d.pos, d.len, buf);
        assert(result == 0);
        assert(memcmp(buf, str, strlen(str)) == 0);
    }

    if (!m_packets.empty())
        return E_FILE_FORMAT_INVALID;

    Packet comment;

    for (;;)
    {
        result = ReadPage(page);
        //TODO: handle underflow

        if (result < 0)  //error
            return result;

        if (GetPacket(comment) >= 1)
            break;
    }

    {
        const char str[] = "\x03vorbis";

        const OggPage::descriptors_t& dd = comment.descriptors;

        const OggPage::Descriptor& d = dd.front();
        assert(d.len > 0);
        assert(size_t(d.len) >= strlen(str));

        void* const p = _malloca(d.len);
        unsigned char* const buf = static_cast<unsigned char*>(p);

        result = m_pReader->Read(d.pos, d.len, buf);
        assert(result == 0);
        assert(memcmp(buf, str, strlen(str)) == 0);
    }

    Packet setup;

    while (GetPacket(setup) < 1)
    {
        result = ReadPage(page);
        //TODO: handle underflow

        if (result < 0)  //error
            return result;
    }

    {
        const char str[] = "\x05vorbis";

        const OggPage::descriptors_t& dd = setup.descriptors;

        const OggPage::Descriptor& d = dd.front();
        assert(d.len > 0);
        assert(size_t(d.len) >= strlen(str));

        void* const p = _malloca(d.len);
        unsigned char* const buf = static_cast<unsigned char*>(p);

        result = m_pReader->Read(d.pos, d.len, buf);
        assert(result == 0);
        assert(memcmp(buf, str, strlen(str)) == 0);
    }

    if (!m_packets.empty())
        return E_FILE_FORMAT_INVALID;

    return 0;
}


long OggStream::ReadPage(OggPage& page)
{
    const long long page_pos = m_pos;

    long result = page.Read(m_pReader, m_pos);

    if (result < 0)  //error
        return result;

    assert(!page.m_descriptors.empty());

    if (page.m_header & OggPage::fContinued)
    {
        if (m_packets.empty())
            return E_FILE_FORMAT_INVALID;

        Packet& pkt = m_packets.back();

        OggPage::descriptors_t& dd = pkt.descriptors;

        if (!dd.empty())  //predicate should always be true
        {
            OggPage::Descriptor& d = dd.back();

            if (d.len >= 0)
                return E_FILE_FORMAT_INVALID;

            d.len = labs(d.len);
        }

        dd.push_back(page.m_descriptors.front());
        page.m_descriptors.pop_front();
    }
    else if (!m_packets.empty())
    {
        const Packet& pkt = m_packets.back();

        const OggPage::descriptors_t& dd = pkt.descriptors;

        if (!dd.empty())  //predicate should always be true
        {
            const OggPage::Descriptor& d = dd.back();

            if (d.len < 0)
                return E_FILE_FORMAT_INVALID;
        }
    }

    OggPage::descriptors_t& dd = page.m_descriptors;

    while (!dd.empty())
    {
        OggPage::Descriptor& d = dd.front();

        m_packets.push_back(Packet());
        Packet& pkt = m_packets.back();

        pkt.descriptors.push_back(d);
        pkt.granule_pos = -1;  //TODO: must set this!

        dd.pop_front();
    }

    assert(!m_packets.empty());

    if (page.m_granule_pos < 0)  //no packet was completed by this page
    {
        const Packet& pkt = m_packets.back();

        const OggPage::descriptors_t& dd = pkt.descriptors;

        if (!dd.empty())  //predicate should always be true
        {
            const OggPage::Descriptor& d = dd.back();

            if (d.len >= 0)  //not incomplete
                return E_FILE_FORMAT_INVALID;
        }

        return 0;  //no granule pos, so nothing else to do just yet
    }

    typedef packets_t::reverse_iterator iter_t;

    iter_t iter = m_packets.rbegin();
    const iter_t iter_end = m_packets.rend();

    while (iter != iter_end)
    {
        Packet& pkt = *iter++;

        OggPage::descriptors_t& dd = pkt.descriptors;

        if (dd.empty())  //weird
            continue;

        OggPage::Descriptor& d = dd.back();

        if (d.len < 0)  //last packet wasn't completed on this cluster
            continue;   //try earlier packet

        if (d.pos <= page_pos)  //we have navigated off of curr page
            return E_FILE_FORMAT_INVALID;

        assert(pkt.granule_pos < 0);

        pkt.granule_pos = page.m_granule_pos;
        return 0;
    }

    return E_FILE_FORMAT_INVALID;

    //Granule pos info:
    //http://lists.xiph.org/pipermail/vorbis/2005-September/025955.html
    //
    //> If this is true:
    //>
    //> "Granule Position Information in Ogg Header is a hint
    //> for the decoder and gives some timing and position
    //> information."
    //>
    //> So say if granule position is 10000, it means that
    //> 10000 PCM samples are encoded in this page
    //> approximately.
    //
    //Incorrect. It means that if you decode up to the end of this page from
    //the beginning of the stream, you'll have a total of precisely 10000
    //samples (assuming all the data was there, and you didn't lose any).
    //Thus it gives precise and absolute positioning information, as
    //required for seeking, for instance.
    //>
    //> If this is true we can neglect this information, it
    //> will not effect the decoding right(but might effect
    //> for streaming)?
    //
    //Incorrect. Vorbis sort of 'overloads' this field; it must be provided
    //to the decoder for correct handling of beginning and end of stream.
    //
    //You also need it for seeking, obviously.

}


long OggStream::Parse()
{
    OggPage page;
    return ReadPage(page);
}


long OggStream::GetPacket(Packet& pkt_)
{
    if (m_packets.empty())
        return 0;  //no packet available for consumption

    const Packet& pkt = m_packets.front();

    const OggPage::descriptors_t& dd = pkt.descriptors;
    assert(!dd.empty());

    const OggPage::Descriptor& d = dd.back();

    if (d.len < 0)  //hasn't been completed yet
        return 0;   //packet not available for consumption yet

    pkt_ = pkt;
    m_packets.pop_front();

    return 1;  //successfully consumed pkt
}


}  //end namespace oggparser