/*
 * HEIF codec.
 * Copyright (c) 2017 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBHEIF_BOX_H
#define LIBHEIF_BOX_H

#include <cstdint>

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <cinttypes>
#include <cstddef>

#include <vector>
#include <string>
#include <memory>
#include <limits>
#include <istream>
#include <bitset>
#include <utility>

#include "error.h"
#include "heif.h"
#include "logging.h"
#include "bitstream.h"

#if !defined(__EMSCRIPTEN__) && !defined(_MSC_VER)
// std::array<bool> is not supported on some older compilers.
#define HAS_BOOL_ARRAY 1
#endif

#define fourcc(id) (((uint32_t)(id[0])<<24) | (id[1]<<16) | (id[2]<<8) | (id[3]))

std::string to_fourcc(uint32_t code);

/*
  constexpr uint32_t fourcc(const char* string)
  {
  return ((string[0]<<24) |
  (string[1]<<16) |
  (string[2]<< 8) |
  (string[3]));
  }
*/


class Fraction
{
public:
  Fraction() = default;

  Fraction(int32_t num, int32_t den);

  // may only use values up to int32_t maximum
  Fraction(uint32_t num, uint32_t den);

  Fraction operator+(const Fraction&) const;

  Fraction operator-(const Fraction&) const;

  Fraction operator+(int) const;

  Fraction operator-(int) const;

  Fraction operator/(int) const;

  int32_t round_down() const;

  int32_t round_up() const;

  int32_t round() const;

  bool is_valid() const;

  int32_t numerator = 0;
  int32_t denominator = 1;
};


inline std::ostream& operator<<(std::ostream& str, const Fraction& f)
{
  str << f.numerator << "/" << f.denominator;
  return str;
}


class BoxHeader
{
public:
  BoxHeader();

  virtual ~BoxHeader() = default;

  constexpr static uint64_t size_until_end_of_file = 0;

  uint64_t get_box_size() const { return m_size; }

  uint32_t get_header_size() const { return m_header_size; }

  uint32_t get_short_type() const { return m_type; }

  std::vector<uint8_t> get_type() const;

  std::string get_type_string() const;

  void set_short_type(uint32_t type) { m_type = type; }


  Error parse_header(BitstreamRange& range);

  virtual std::string dump(Indent&) const;


  virtual bool is_full_box_header() const { return false; }


private:
  uint64_t m_size = 0;

  uint32_t m_type = 0;
  std::vector<uint8_t> m_uuid_type;

protected:
  uint32_t m_header_size = 0;
};


class Box : public BoxHeader
{
public:
  Box() = default;

  void set_short_header(const BoxHeader& hdr)
  {
    *(BoxHeader*) this = hdr;
  }

  // header size without the FullBox fields (if applicable)
  int calculate_header_size(bool data64bit) const;

  static Error read(BitstreamRange& range, std::shared_ptr<Box>* box);

  virtual Error write(StreamWriter& writer) const;

  // check, which box version is required and set this in the (full) box header
  virtual void derive_box_version() {}

  void derive_box_version_recursive();

  std::string dump(Indent&) const override;

  std::shared_ptr<Box> get_child_box(uint32_t short_type) const;

  std::vector<std::shared_ptr<Box>> get_child_boxes(uint32_t short_type) const;

  template<typename T>
  std::vector<std::shared_ptr<T>> get_typed_child_boxes(uint32_t short_type) const
  {
    auto boxes = get_child_boxes(short_type);
    std::vector<std::shared_ptr<T>> typedBoxes;
    for (const auto& box : boxes) {
      typedBoxes.push_back(std::dynamic_pointer_cast<T>(box));
    }
    return typedBoxes;
  }

  const std::vector<std::shared_ptr<Box>>& get_all_child_boxes() const { return m_children; }

  int append_child_box(const std::shared_ptr<Box>& box)
  {
    m_children.push_back(box);
    return (int) m_children.size() - 1;
  }

protected:
  virtual Error parse(BitstreamRange& range);

  std::vector<std::shared_ptr<Box>> m_children;

  const static int READ_CHILDREN_ALL = -1;

  Error read_children(BitstreamRange& range, int number = READ_CHILDREN_ALL);

  Error write_children(StreamWriter& writer) const;

  std::string dump_children(Indent&) const;


  // --- writing

  virtual size_t reserve_box_header_space(StreamWriter& writer, bool data64bit = false) const;

  Error prepend_header(StreamWriter&, size_t box_start, bool data64bit = false) const;

  virtual Error write_header(StreamWriter&, size_t total_box_size, bool data64bit = false) const;
};


class FullBox : public Box
{
public:
  bool is_full_box_header() const override { return true; }

  std::string dump(Indent& indent) const override;

  void derive_box_version() override { set_version(0); }


  Error parse_full_box_header(BitstreamRange& range);

  uint8_t get_version() const { return m_version; }

  void set_version(uint8_t version) { m_version = version; }

  uint32_t get_flags() const { return m_flags; }

  void set_flags(uint32_t flags) { m_flags = flags; }

protected:

  // --- writing

  size_t reserve_box_header_space(StreamWriter& writer, bool data64bit = false) const override;

  Error write_header(StreamWriter&, size_t total_size, bool data64bit = false) const override;


private:
  uint8_t m_version = 0;
  uint32_t m_flags = 0;
};


class Box_ftyp : public Box
{
public:
  Box_ftyp()
  {
    set_short_type(fourcc("ftyp"));
  }

  std::string dump(Indent&) const override;

  bool has_compatible_brand(uint32_t brand) const;

  std::vector<uint32_t> list_brands() const { return m_compatible_brands; }

  void set_major_brand(uint32_t major_brand) { m_major_brand = major_brand; }

  void set_minor_version(uint32_t minor_version) { m_minor_version = minor_version; }

  void clear_compatible_brands() { m_compatible_brands.clear(); }

  void add_compatible_brand(uint32_t brand);

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  uint32_t m_major_brand = 0;
  uint32_t m_minor_version = 0;
  std::vector<uint32_t> m_compatible_brands;
};


class Box_meta : public FullBox
{
public:
  Box_meta()
  {
    set_short_type(fourcc("meta"));
  }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_hdlr : public FullBox
{
public:
  Box_hdlr()
  {
    set_short_type(fourcc("hdlr"));
  }

  std::string dump(Indent&) const override;

  uint32_t get_handler_type() const { return m_handler_type; }

  void set_handler_type(uint32_t handler) { m_handler_type = handler; }

  Error write(StreamWriter& writer) const override;

  void set_name(std::string name) { m_name = std::move(name); }

protected:
  Error parse(BitstreamRange& range) override;

private:
  uint32_t m_pre_defined = 0;
  uint32_t m_handler_type = fourcc("pict");
  uint32_t m_reserved[3] = {0,};
  std::string m_name;
};


class Box_pitm : public FullBox
{
public:
  Box_pitm()
  {
    set_short_type(fourcc("pitm"));
  }

  std::string dump(Indent&) const override;

  heif_item_id get_item_ID() const { return m_item_ID; }

  void set_item_ID(heif_item_id id) { m_item_ID = id; }

  void derive_box_version() override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  heif_item_id m_item_ID = 0;
};


class Box_iloc : public FullBox
{
public:
  Box_iloc()
  {
    set_short_type(fourcc("iloc"));
  }

  std::string dump(Indent&) const override;

  struct Extent
  {
    uint64_t index = 0;
    uint64_t offset = 0;
    uint64_t length = 0;

    std::vector<uint8_t> data; // only used when writing data
  };

  struct Item
  {
    heif_item_id item_ID = 0;
    uint8_t construction_method = 0; // >= version 1
    uint16_t data_reference_index = 0;
    uint64_t base_offset = 0;

    std::vector<Extent> extents;
  };

  const std::vector<Item>& get_items() const { return m_items; }

  Error read_data(const Item& item,
                  const std::shared_ptr<StreamReader>& istr,
                  const std::shared_ptr<class Box_idat>&,
                  std::vector<uint8_t>* dest) const;

  void set_min_version(uint8_t min_version) { m_user_defined_min_version = min_version; }

  // append bitstream data that will be written later (after iloc box)
  Error append_data(heif_item_id item_ID,
                    const std::vector<uint8_t>& data,
                    uint8_t construction_method = 0);

  // append bitstream data that already has been written (before iloc box)
  // Error write_mdat_before_iloc(heif_image_id item_ID,
  //                              std::vector<uint8_t>& data)

  // reserve data entry that will be written later
  // Error reserve_mdat_item(heif_image_id item_ID,
  //                         uint8_t construction_method,
  //                         uint32_t* slot_ID);
  // void patch_mdat_slot(uint32_t slot_ID, size_t start, size_t length);

  void derive_box_version() override;

  Error write(StreamWriter& writer) const override;

  Error write_mdat_after_iloc(StreamWriter& writer);

protected:
  Error parse(BitstreamRange& range) override;

private:
  std::vector<Item> m_items;

  mutable size_t m_iloc_box_start = 0;
  uint8_t m_user_defined_min_version = 0;
  uint8_t m_offset_size = 0;
  uint8_t m_length_size = 0;
  uint8_t m_base_offset_size = 0;
  uint8_t m_index_size = 0;

  void patch_iloc_header(StreamWriter& writer) const;

  int m_idat_offset = 0; // only for writing: offset of next data array
};


class Box_infe : public FullBox
{
public:
  Box_infe()
  {
    set_short_type(fourcc("infe"));
  }

  std::string dump(Indent&) const override;

  bool is_hidden_item() const { return m_hidden_item; }

  void set_hidden_item(bool hidden);

  heif_item_id get_item_ID() const { return m_item_ID; }

  void set_item_ID(heif_item_id id) { m_item_ID = id; }

  const std::string& get_item_type() const { return m_item_type; }

  void set_item_type(const std::string& type) { m_item_type = type; }

  void set_item_name(const std::string& name) { m_item_name = name; }

  const std::string& get_content_type() const { return m_content_type; }

  const std::string& get_content_encoding() const { return m_content_encoding; }

  void set_content_type(const std::string& content_type) { m_content_type = content_type; }

  void set_content_encoding(const std::string& content_encoding) { m_content_encoding = content_encoding; }

  void derive_box_version() override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  heif_item_id m_item_ID = 0;
  uint16_t m_item_protection_index = 0;

  std::string m_item_type;
  std::string m_item_name;
  std::string m_content_type;
  std::string m_content_encoding;
  std::string m_item_uri_type;

  // if set, this item should not be part of the presentation (i.e. hidden)
  bool m_hidden_item = false;
};


class Box_iinf : public FullBox
{
public:
  Box_iinf()
  {
    set_short_type(fourcc("iinf"));
  }

  std::string dump(Indent&) const override;

  void derive_box_version() override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  //std::vector< std::shared_ptr<Box_infe> > m_iteminfos;
};


class Box_iprp : public Box
{
public:
  Box_iprp()
  {
    set_short_type(fourcc("iprp"));
  }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_ipco : public Box
{
public:
  Box_ipco()
  {
    set_short_type(fourcc("ipco"));
  }

  Error get_properties_for_item_ID(heif_item_id itemID,
                                   const std::shared_ptr<class Box_ipma>&,
                                   std::vector<std::shared_ptr<Box>>& out_properties) const;

  std::shared_ptr<Box> get_property_for_item_ID(heif_item_id itemID,
                                                const std::shared_ptr<class Box_ipma>&,
                                                uint32_t property_box_type) const;

  bool is_property_essential_for_item(heif_item_id itemId,
                                      const std::shared_ptr<const class Box>& property,
                                      const std::shared_ptr<class Box_ipma>&) const;

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_ispe : public FullBox
{
public:
  Box_ispe()
  {
    set_short_type(fourcc("ispe"));
  }

  uint32_t get_width() const { return m_image_width; }

  uint32_t get_height() const { return m_image_height; }

  void set_size(uint32_t width, uint32_t height)
  {
    m_image_width = width;
    m_image_height = height;
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  uint32_t m_image_width = 0;
  uint32_t m_image_height = 0;
};


class Box_ipma : public FullBox
{
public:
  Box_ipma()
  {
    set_short_type(fourcc("ipma"));
  }

  std::string dump(Indent&) const override;

  struct PropertyAssociation
  {
    bool essential;
    uint16_t property_index;
  };

  const std::vector<PropertyAssociation>* get_properties_for_item_ID(heif_item_id itemID) const;

  bool is_property_essential_for_item(heif_item_id itemId, int propertyIndex) const;

  void add_property_for_item_ID(heif_item_id itemID,
                                PropertyAssociation assoc);

  void derive_box_version() override;

  Error write(StreamWriter& writer) const override;

  void insert_entries_from_other_ipma_box(const Box_ipma& b);

protected:
  Error parse(BitstreamRange& range) override;

  struct Entry
  {
    heif_item_id item_ID;
    std::vector<PropertyAssociation> associations;
  };

  std::vector<Entry> m_entries;
};


class Box_auxC : public FullBox
{
public:
  Box_auxC()
  {
    set_short_type(fourcc("auxC"));
  }

  const std::string& get_aux_type() const { return m_aux_type; }

  void set_aux_type(const std::string& type) { m_aux_type = type; }

  const std::vector<uint8_t>& get_subtypes() const { return m_aux_subtypes; }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;

  Error write(StreamWriter& writer) const override;

private:
  std::string m_aux_type;
  std::vector<uint8_t> m_aux_subtypes;
};


class Box_irot : public Box
{
public:
  Box_irot()
  {
    set_short_type(fourcc("irot"));
  }

  std::string dump(Indent&) const override;

  int get_rotation() const { return m_rotation; }

  // Only these multiples of 90 are allowed: 0, 90, 180, 270.
  void set_rotation_ccw(int rot) { m_rotation = rot; }

protected:
  Error parse(BitstreamRange& range) override;

  Error write(StreamWriter& writer) const override;

private:
  int m_rotation = 0; // in degrees (CCW)
};


class Box_imir : public Box
{
public:
  Box_imir()
  {
    set_short_type(fourcc("imir"));
  }

  heif_transform_mirror_direction get_mirror_direction() const { return m_axis; }

  void set_mirror_direction(heif_transform_mirror_direction dir) { m_axis = dir; }

  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;

  Error write(StreamWriter& writer) const override;

private:
  heif_transform_mirror_direction m_axis = heif_transform_mirror_direction_vertical;
};


class Box_clap : public Box
{
public:
  Box_clap()
  {
    set_short_type(fourcc("clap"));
  }

  std::string dump(Indent&) const override;

  int left_rounded(int image_width) const;  // first column
  int right_rounded(int image_width) const; // last column that is part of the cropped image
  int top_rounded(int image_height) const;   // first row
  int bottom_rounded(int image_height) const; // last row included in the cropped image

  int get_width_rounded() const;

  int get_height_rounded() const;

  void set(uint32_t clap_width, uint32_t clap_height,
           uint32_t image_width, uint32_t image_height);

protected:
  Error parse(BitstreamRange& range) override;

  Error write(StreamWriter& writer) const override;

private:
  Fraction m_clean_aperture_width;
  Fraction m_clean_aperture_height;
  Fraction m_horizontal_offset;
  Fraction m_vertical_offset;
};


class Box_iref : public FullBox
{
public:
  Box_iref()
  {
    set_short_type(fourcc("iref"));
  }

  struct Reference
  {
    BoxHeader header;

    heif_item_id from_item_ID;
    std::vector<heif_item_id> to_item_ID;
  };


  std::string dump(Indent&) const override;

  bool has_references(heif_item_id itemID) const;

  std::vector<heif_item_id> get_references(heif_item_id itemID, uint32_t ref_type) const;

  std::vector<Reference> get_references_from(heif_item_id itemID) const;

  void add_reference(heif_item_id from_id, uint32_t type, const std::vector<heif_item_id>& to_ids);

protected:
  Error parse(BitstreamRange& range) override;

  Error write(StreamWriter& writer) const override;

  void derive_box_version() override;

private:
  std::vector<Reference> m_references;
};


class Box_hvcC : public Box
{
public:
  Box_hvcC()
  {
    set_short_type(fourcc("hvcC"));
  }

  struct configuration
  {
    uint8_t configuration_version;
    uint8_t general_profile_space;
    bool general_tier_flag;
    uint8_t general_profile_idc;
    uint32_t general_profile_compatibility_flags;

    static const int NUM_CONSTRAINT_INDICATOR_FLAGS = 48;
    std::bitset<NUM_CONSTRAINT_INDICATOR_FLAGS> general_constraint_indicator_flags;

    uint8_t general_level_idc;

    uint16_t min_spatial_segmentation_idc;
    uint8_t parallelism_type;
    uint8_t chroma_format;
    uint8_t bit_depth_luma;
    uint8_t bit_depth_chroma;
    uint16_t avg_frame_rate;

    uint8_t constant_frame_rate;
    uint8_t num_temporal_layers;
    uint8_t temporal_id_nested;
  };


  std::string dump(Indent&) const override;

  bool get_headers(std::vector<uint8_t>* dest) const;

  void set_configuration(const configuration& config) { m_configuration = config; }

  const configuration& get_configuration() const { return m_configuration; }

  void append_nal_data(const std::vector<uint8_t>& nal);

  void append_nal_data(const uint8_t* data, size_t size);

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  struct NalArray
  {
    uint8_t m_array_completeness;
    uint8_t m_NAL_unit_type;

    std::vector<std::vector<uint8_t> > m_nal_units;
  };

  configuration m_configuration;
  uint8_t m_length_size = 4; // default: 4 bytes for NAL unit lengths

  std::vector<NalArray> m_nal_array;
};


class Box_av1C : public Box
{
public:
  Box_av1C()
  {
    set_short_type(fourcc("av1C"));
  }

  struct configuration
  {
    //unsigned int (1) marker = 1;
    uint8_t version = 1;
    uint8_t seq_profile = 0;
    uint8_t seq_level_idx_0 = 0;
    uint8_t seq_tier_0 = 0;
    uint8_t high_bitdepth = 0;
    uint8_t twelve_bit = 0;
    uint8_t monochrome = 0;
    uint8_t chroma_subsampling_x = 0;
    uint8_t chroma_subsampling_y = 0;
    uint8_t chroma_sample_position = 0;
    //uint8_t reserved = 0;

    uint8_t initial_presentation_delay_present = 0;
    uint8_t initial_presentation_delay_minus_one = 0;

    //unsigned int (8)[] configOBUs;
  };


  std::string dump(Indent&) const override;

  bool get_headers(std::vector<uint8_t>* dest) const
  {
    *dest = m_config_OBUs;
    return true;
  }

  void set_configuration(const configuration& config) { m_configuration = config; }

  const configuration& get_configuration() const { return m_configuration; }

  //void append_nal_data(const std::vector<uint8_t>& nal);
  //void append_nal_data(const uint8_t* data, size_t size);

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  configuration m_configuration;

  std::vector<uint8_t> m_config_OBUs;
};


class Box_vvcC : public Box
{
public:
  Box_vvcC()
  {
    set_short_type(fourcc("vvcC"));
  }

  struct configuration
  {
    uint8_t configurationVersion = 1;
    uint16_t avgFrameRate_times_256;
    uint8_t constantFrameRate;
    uint8_t numTemporalLayers;
    uint8_t lengthSize;
    bool ptl_present_flag;
    //if (ptl_present_flag) {
    //  VvcPTLRecord(numTemporalLayers) track_ptl;
    //  uint16_t output_layer_set_idx;
    //}
    bool chroma_format_present_flag;
    uint8_t chroma_format_idc;

    bool bit_depth_present_flag;
    uint8_t bit_depth;

    uint8_t numOfArrays;
#if 0
    for (j=0; j < numOfArrays; j++) {
      unsigned int(1) array_completeness;
      bit(1) reserved = 0;
      unsigned int(6) NAL_unit_type;
      unsigned int(16) numNalus;
      for (i=0; i< numNalus; i++) {
        unsigned int(16) nalUnitLength;
        bit(8*nalUnitLength) nalUnit;
      }
    }
#endif
  };


  std::string dump(Indent&) const override;

  bool get_headers(std::vector<uint8_t>* dest) const
  {
    *dest = m_config_NALs;
    return true;
  }

  void set_configuration(const configuration& config) { m_configuration = config; }

  const configuration& get_configuration() const { return m_configuration; }

  //void append_nal_data(const std::vector<uint8_t>& nal);
  //void append_nal_data(const uint8_t* data, size_t size);

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  configuration m_configuration;

  std::vector<uint8_t> m_config_NALs;
};


class Box_idat : public Box
{
public:
  std::string dump(Indent&) const override;

  Error read_data(const std::shared_ptr<StreamReader>& istr,
                  uint64_t start, uint64_t length,
                  std::vector<uint8_t>& out_data) const;

  int append_data(const std::vector<uint8_t>& data)
  {
    auto pos = m_data_for_writing.size();

    m_data_for_writing.insert(m_data_for_writing.end(),
                              data.begin(),
                              data.end());

    return (int) pos;
  }

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

  std::streampos m_data_start_pos;

  std::vector<uint8_t> m_data_for_writing;
};


class Box_grpl : public Box
{
public:
  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;

  struct EntityGroup
  {
    FullBox header;
    uint32_t group_id;

    std::vector<heif_item_id> entity_ids;
  };

  std::vector<EntityGroup> m_entity_groups;
};


class Box_dinf : public Box
{
public:
  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_dref : public FullBox
{
public:
  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_url : public FullBox
{
public:
  std::string dump(Indent&) const override;

protected:
  Error parse(BitstreamRange& range) override;

  std::string m_location;
};

class Box_pixi : public FullBox
{
public:
  Box_pixi()
  {
    set_short_type(fourcc("pixi"));
  }

  int get_num_channels() const { return (int) m_bits_per_channel.size(); }

  int get_bits_per_channel(int channel) const { return m_bits_per_channel[channel]; }

  void add_channel_bits(uint8_t c)
  {
    m_bits_per_channel.push_back(c);
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  std::vector<uint8_t> m_bits_per_channel;
};


class Box_pasp : public Box
{
public:
  Box_pasp()
  {
    set_short_type(fourcc("pasp"));
  }

  uint32_t hSpacing = 1;
  uint32_t vSpacing = 1;

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_lsel : public Box
{
public:
  Box_lsel()
  {
    set_short_type(fourcc("lsel"));
  }

  uint16_t layer_id = 0;

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_a1op : public Box
{
public:
  Box_a1op()
  {
    set_short_type(fourcc("a1op"));
  }

  uint8_t op_index = 0;

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_a1lx : public Box
{
public:
  Box_a1lx()
  {
    set_short_type(fourcc("a1lx"));
  }

  uint32_t layer_size[3]{};

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_clli : public Box
{
public:
  Box_clli()
  {
    set_short_type(fourcc("clli"));
  }

  heif_content_light_level clli;

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class Box_mdcv : public Box
{
public:
  Box_mdcv()
  {
    set_short_type(fourcc("mdcv"));
  }

  heif_mastering_display_colour_volume mdcv;

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;
};


class color_profile
{
public:
  virtual ~color_profile() = default;

  virtual uint32_t get_type() const = 0;

  virtual std::string dump(Indent&) const = 0;

  virtual Error write(StreamWriter& writer) const = 0;
};

class color_profile_raw : public color_profile
{
public:
  color_profile_raw(uint32_t type, const std::vector<uint8_t>& data)
      : m_type(type), m_data(data) {}

  uint32_t get_type() const override { return m_type; }

  const std::vector<uint8_t>& get_data() const { return m_data; }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

private:
  uint32_t m_type;
  std::vector<uint8_t> m_data;
};


class color_profile_nclx : public color_profile
{
public:
  color_profile_nclx() { set_default(); }

  uint32_t get_type() const override { return fourcc("nclx"); }

  std::string dump(Indent&) const override;

  Error parse(BitstreamRange& range);

  Error write(StreamWriter& writer) const override;

  uint16_t get_colour_primaries() const { return m_colour_primaries; }

  uint16_t get_transfer_characteristics() const { return m_transfer_characteristics; }

  uint16_t get_matrix_coefficients() const { return m_matrix_coefficients; }

  bool get_full_range_flag() const { return m_full_range_flag; }

  void set_colour_primaries(uint16_t primaries) { m_colour_primaries = primaries; }

  void set_transfer_characteristics(uint16_t characteristics) { m_transfer_characteristics = characteristics; }

  void set_matrix_coefficients(uint16_t coefficients) { m_matrix_coefficients = coefficients; }

  void set_full_range_flag(bool full_range) { m_full_range_flag = full_range; }

  void set_default();

  void set_undefined();

  Error get_nclx_color_profile(struct heif_color_profile_nclx** out_data) const;

  static struct heif_color_profile_nclx* alloc_nclx_color_profile();

  static void free_nclx_color_profile(struct heif_color_profile_nclx* profile);

  void set_from_heif_color_profile_nclx(const struct heif_color_profile_nclx* nclx);

private:
  uint16_t m_colour_primaries = heif_color_primaries_unspecified;
  uint16_t m_transfer_characteristics = heif_transfer_characteristic_unspecified;
  uint16_t m_matrix_coefficients = heif_matrix_coefficients_unspecified;
  bool m_full_range_flag = true;
};


class Box_colr : public Box
{
public:
  Box_colr()
  {
    set_short_type(fourcc("colr"));
  }

  std::string dump(Indent&) const override;

  uint32_t get_color_profile_type() const { return m_color_profile->get_type(); }

  const std::shared_ptr<const color_profile>& get_color_profile() const { return m_color_profile; }

  void set_color_profile(const std::shared_ptr<const color_profile>& prof) { m_color_profile = prof; }


  Error write(StreamWriter& writer) const override;

protected:
  Error parse(BitstreamRange& range) override;

private:
  std::shared_ptr<const color_profile> m_color_profile;
};

/**
 * User Description property.
 *
 * Permits the association of items or entity groups with a user-defined name, description and tags;
 * there may be multiple udes properties, each with a different language code.
 *
 * See ISO/IEC 23008-12:2022(E) Section 6.5.20.
 */
class Box_udes : public FullBox
{
public:
  Box_udes()
  {
    set_short_type(fourcc("udes"));
  }

  std::string dump(Indent&) const override;

  Error write(StreamWriter& writer) const override;

  /**
   * Language tag.
   *
   * An RFC 5646 compliant language identifier for the language of the text contained in the other properties.
   * Examples: "en-AU", "de-DE", or "zh-CN“.
   * When is empty, the language is unknown or not undefined.
   */
  std::string get_lang() const { return m_lang; }

  /**
   * Set the language tag.
   *
   * An RFC 5646 compliant language identifier for the language of the text contained in the other properties.
   * Examples: "en-AU", "de-DE", or "zh-CN“.
   */
  void set_lang(const std::string lang) { m_lang = lang; }

  /**
   * Name.
   *
   * Human readable name for the item or group being described.
   * May be empty, indicating no name is applicable.
   */
  std::string get_name() const { return m_name; }

  /**
  * Set the name.
  *
  * Human readable name for the item or group being described.
  */
  void set_name(const std::string name) { m_name = name; }

  /**
   * Description.
   *
   * Human readable description for the item or group.
   * May be empty, indicating no description has been provided.
   */
  std::string get_description() const { return m_description; }

  /**
   * Set the description.
   *
   * Human readable description for the item or group.
   */
  void set_description(const std::string description) { m_description = description; }

  /**
   * Tags.
   *
   * Comma separated user defined tags applicable to the item or group.
   * May be empty, indicating no tags have been assigned.
   */
  std::string get_tags() const { return m_tags; }

  /**
   * Set the tags.
   *
   * Comma separated user defined tags applicable to the item or group.
   */
  void set_tags(const std::string tags) { m_tags = tags; }

protected:
  Error parse(BitstreamRange& range) override;

private:
  std::string m_lang;
  std::string m_name;
  std::string m_description;
  std::string m_tags;
};

#endif
