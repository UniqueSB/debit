/*
 * Copyright (C) 2006, 2007 Jean-Baptiste Note <jean-baptiste.note@m4x.org>
 *
 * This file is part of debit.
 *
 * Debit is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Debit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with debit.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <glib.h>

#include "keyfile.h"

#include "sites.h"
#include "design.h"

/*
 * File site control description is of form
 * [XC2V2000]
 * X_WIDTH=;
 * Y_WIDTH=;
 * File site description data is of this form
 * [SITENAME]
 * ID=NUM;
 * XINTERVALS=1,2,3,4
 * YINTERVALS=2,3,7,7
 * This means: sites in ([1,2] u [3,4]) x ([2,3] u [7,7]) are of type ID
 * Xilinx is cool: the names are okay so that this description works and
 * we don't have to repeat sitename several times
 */

#if defined(VIRTEX2)

static const gchar *
chipfiles[XC2__NUM] = {
  [XC2V40] = "xc2v40",
  [XC2V80] = "xc2v80",
  [XC2V250] = "xc2v250",
  [XC2V500] = "xc2v500",
  [XC2V1000] = "xc2v1000",
  [XC2V1500] = "xc2v1500",
  [XC2V2000] = "xc2v2000",
  [XC2V3000] = "xc2v3000",
  [XC2V4000] = "xc2v4000",
  [XC2V6000] = "xc2v6000",
  [XC2V8000] = "xc2v8000",
};

#elif defined(SPARTAN3)

static const gchar *
chipfiles[XC3__NUM] = {
  [XC3S50] = "xc3s50",
  [XC3S200] = "xc3s200",
  [XC3S400] = "xc3s400",
  [XC3S1000] = "xc3s1000",
  [XC3S1500] = "xc3s1500",
  [XC3S2000] = "xc3s2000",
  [XC3S4000] = "xc3s4000",
  [XC3S5000] = "xc3s5000",
};

#elif defined(VIRTEX4)

static const gchar *
chipfiles[XC4VLX__NUM] = {
  [XC4VLX15] = "xc4vlx15",
  [XC4VLX25] = "xc4vlx25",
  [XC4VLX40] = "xc4vlx40",
  [XC4VLX60] = "xc4vlx60",
  [XC4VLX80] = "xc4vlx80",
  [XC4VLX100] = "xc4vlx100",
  [XC4VLX160] = "xc4vlx160",
  [XC4VLX200] = "xc4vlx200",
};

#elif defined(VIRTEX5)

static const gchar *
chipfiles[XC5VLX__NUM] = {
  [XC5VLX30] = "xc5vlx30",
  [XC5VLX50] = "xc5vlx50",
  [XC5VLX85] = "xc5vlx85",
  [XC5VLX110] = "xc5vlx110",
  [XC5VLX220] = "xc5vlx220",
  [XC5VLX330] = "xc5vlx330",
};

#endif

/* iterate over intervals */
typedef void (* interval_iterator_t)(unsigned i, void *dat);

static void
iterate_over_intervals(interval_iterator_t iter, void *dat,
		       const gint *intervals, const gsize len) {
  unsigned i;
  for (i = 0; i < len-1; i+=2) {
    unsigned j,
      min = intervals[i],
      max = intervals[i+1];
    /* iterate over the interval */
    for (j = min; j < max; j++)
      iter(j, dat);
  }
}

typedef struct _line_iterator {
  chip_descr_t *chip;
  site_type_t type;
  const gint *intervals_x;
  unsigned xlen;
  /* current line we're operating at */
  unsigned y;
} line_iterator_t;

static void
pos_setting(unsigned x, void *dat) {
  line_iterator_t *data = dat;
  unsigned y = data->y;
  chip_descr_t *chip = data->chip;
  site_type_t type = data->type;
  csite_descr_t *descr = get_global_site(chip,x,y);

  g_assert(descr->type == SITE_TYPE_NEUTRAL);
  descr->type = type;
}

static void
iterate_over_lines(unsigned y, void *dat) {
  /* dat contains the y-coordinate */
  line_iterator_t *data = dat;
  data->y = y;
  iterate_over_intervals(pos_setting, dat,
			 data->intervals_x, data->xlen);
}

static void
init_chip_type(chip_descr_t *chip, site_type_t type,
	       const gint *intervals_x, const gsize xlen,
	       const gint *intervals_y, const gsize ylen) {
  line_iterator_t x_arg = {
    .type = type, .chip = chip,
    .intervals_x = intervals_x, .xlen = xlen,
  };
  iterate_over_intervals(iterate_over_lines, &x_arg,
			 intervals_y, ylen);
}

static inline void
alloc_chip(chip_descr_t *descr) {
  unsigned nelems = descr->width * descr->height;
  descr->data = g_new0(csite_descr_t, nelems);
  g_datalist_init(&descr->lookup);
}

static inline void
free_chip(chip_descr_t *descr) {
  g_datalist_clear(&descr->lookup);
  g_free(descr->data);
  descr->data = NULL;
}

static inline void
alloc_nchip(chip_descr_t *descr) {
  unsigned aelems = descr->awidth * descr->aheight;
  descr->area = g_new0(nsite_area_t, aelems);
  descr->x_descr = g_new0(interval_t, descr->awidth + 1);
  descr->y_descr = g_new0(interval_t, descr->aheight + 1);
  g_warning("allocated chip %u X %u", descr->awidth, descr->aheight);
}

#define FREE_FIELD(str, field) {		\
    void *field = (void *)str->field;		\
    str->field = NULL;				\
    g_free(field);				\
}

static inline void
free_nchip(chip_descr_t *descr) {
  FREE_FIELD(descr, area);
  FREE_FIELD(descr, x_descr);
  FREE_FIELD(descr, y_descr);
}

static void
init_group_chip_type(GKeyFile *file, const gchar *group,
		     gpointer data) {
  chip_descr_t *chip = data;
  GError *error = NULL;
  gint *intervals[2] = {NULL, NULL};
  gsize sizes[2];
  unsigned type;

  /* get group name & al from the group, fill in with this */
  intervals[0] = g_key_file_get_integer_list(file, group, "x", &sizes[0], &error);
  if (error)
    goto out_err;
  intervals[1] = g_key_file_get_integer_list(file, group, "y", &sizes[1], &error);
  if (error)
    goto out_err;
  type = g_key_file_get_integer(file, group, "type", &error);
  if (error)
    goto out_err;
  g_assert(type < NR_SITE_TYPE);
  init_chip_type(chip, type, intervals[0], sizes[0], intervals[1], sizes[1]);

 out_err:
  if (intervals[0])
    g_free(intervals[0]);
  if (intervals[1])
    g_free(intervals[1]);
  if (error) {
    g_warning("Error treating group %s: %s",
	      group, error->message);
    g_error_free(error);
  }
  return;
}

void
iterate_over_sites(const chip_descr_t *chip,
		   site_iterator_t fun, gpointer data) {
  unsigned x, y;
  unsigned xmax = chip->width, ymax = chip->height;
  csite_descr_t *site = chip->data;

  for (y = 0; y < ymax; y++)
    for (x = 0; x < xmax; x++)
      fun(x,y,site++,data);
}

void
iterate_over_typed_sites(const chip_descr_t *chip, site_type_t type,
			 site_iterator_t fun, gpointer data) {
  unsigned x, y;
  unsigned xmax = chip->width, ymax = chip->height;
  csite_descr_t *site = chip->data;

  for (y = 0; y < ymax; y++)
    for (x = 0; x < xmax; x++) {
      if (site->type == type)
	fun(x,y,site,data);
      site++;
    }
}

typedef struct _local_counter {
  gint x;
  gint y;
  gint y_global;
} local_counter_t;

static void
init_site_coord(unsigned site_x, unsigned site_y,
		csite_descr_t *site, gpointer dat) {
  local_counter_t *coords = dat;
  site_type_t type = site->type;
  local_counter_t *count = coords + type;
  gint x = count->x, y = count->y, y_global = count->y_global;
  gboolean newline = y_global < (int)site_y ? TRUE : FALSE;

  (void) site_x;
  (void) site_y;

  if (newline) {
    y++;
    x = 0;
  } else {
    x++;
  }

  site->type_coord.x = x;
  site->type_coord.y = y;

  if (newline)
    count->y_global = site_y;
  count->x = x;
  count->y = y;
}

static void
init_local_coordinates(chip_descr_t *chip) {
  /* local counters */
  local_counter_t coords[NR_SITE_TYPE];
  unsigned i;
  for (i = 0; i < NR_SITE_TYPE; i++) {
    coords[i].y = -1;
    coords[i].y_global = -1;
  }
  iterate_over_sites(chip, init_site_coord, coords);
}

static void
fill_lookup(unsigned x, unsigned y,
	    csite_descr_t *site, gpointer dat) {
  chip_descr_t *chip = dat;
  gchar name[MAX_SITE_NLEN];
  snprint_csite(name, ARRAY_SIZE(name), site, x, y);
  g_datalist_set_data(&chip->lookup, name, site);
  return;
}

static inline void
init_lookup(chip_descr_t *chip) {
  iterate_over_sites(chip, fill_lookup, chip);
}

static void
init_chip(chip_descr_t *chip, GKeyFile *file) {
  /* for each of the types, call the init functions */
  iterate_over_groups(file, init_group_chip_type, chip);
  init_local_coordinates(chip);
  init_lookup(chip);
}

static inline void
write_same(unsigned *pos, const unsigned val) {
  if (*pos != 0 && *pos != val) {
    g_warning("replacing %u with %u", *pos, val);
  }
  g_assert(*pos == 0 || *pos == val);
  *pos = val;
}

static inline void
fill_interval(interval_t *descr,  const gsize dlen,
	      const gint *idx_descr,
	      const gint *intervals,
	      const gsize len) {
  gsize i;
  (void) intervals;
  for (i = 0; i < len; i++) {
    const unsigned idx = idx_descr[i];
    const unsigned idx2 = i << 1;
    (void) idx2;
    g_assert(idx < dlen);
    g_warning("writing to interval index %u for i=%" G_GSIZE_FORMAT, idx, i);
    write_same(&descr[idx].base, intervals[idx2]);
    write_same(&descr[idx].length, intervals[idx2 + 1] - intervals[idx2]);
  }
  descr[dlen].base = descr[dlen-1].base + descr[dlen-1].length;
  descr[dlen].length = 0;
}

static inline void
fill_area(nsite_area_t *area,
	  const site_type_t type, const gsize width,
	  const gint *descr_x, const interval_t *x_descr, const gsize dxlen,
	  const gint *descr_y, const interval_t *y_descr, const gsize dylen) {
  unsigned i,j;
  unsigned xpos = 1, ypos = 1;

  for (j = 0; j < dylen; j++) {
    unsigned yidx = descr_y[j];
    for (i = 0; i < dxlen; i++) {
      unsigned xidx = descr_x[i];
      unsigned pos = xidx + width * yidx;
      area[pos].type = type;
      area[pos].type_base.x = xpos;
      area[pos].type_base.y = ypos;
      xpos += x_descr[xidx].length;
    }
    xpos = 1;
    ypos += y_descr[yidx].length;
  }
}

static void
init_nchip_type(chip_descr_t *chip, site_type_t type,
		const gint *descr_x, const gint *intervals_x, const gsize dxlen,
		const gint *descr_y, const gint *intervals_y, const gsize dylen) {
  /* XXX do this properly */
  nsite_area_t *area = (void *) chip->area;
  interval_t *x_descr = (void *) chip->x_descr;
  interval_t *y_descr = (void *) chip->y_descr;

  /* fill/check the x and y descriptors */
  g_warning("X descr");
  fill_interval(x_descr, chip->awidth, descr_x, intervals_x, dxlen);
  g_warning("Y descr");
  fill_interval(y_descr, chip->aheight, descr_y, intervals_y, dylen);
  /* Then fill the area array for this type */
  fill_area(area, type, chip->awidth,
	    descr_x, x_descr, dxlen,
	    descr_y, y_descr, dylen);
}

static void
init_area_chip_type(GKeyFile *file, const gchar *group,
		    gpointer data) {
  chip_descr_t *chip = data;
  GError *error = NULL;
  gint *intervals[2] = {NULL, NULL};
  gint *geom[2] = {NULL, NULL};
  gsize sizes[2], geomsizes[2];
  unsigned type;

  /* get group name & al from the group, fill in with this */
  intervals[0] = g_key_file_get_integer_list(file, group, "x", &sizes[0], &error);
  if (error)
    goto out_err;
  geom[0] = g_key_file_get_integer_list(file, group, "xpos", &geomsizes[0], &error);
  g_assert(geomsizes[0] * 2 == sizes[0]);
  if (error)
    goto out_err;
  intervals[1] = g_key_file_get_integer_list(file, group, "y", &sizes[1], &error);
  if (error)
    goto out_err;
  geom[1] = g_key_file_get_integer_list(file, group, "ypos", &geomsizes[1], &error);
  g_assert(geomsizes[1] * 2 == sizes[1]);
  if (error)
    goto out_err;
  type = g_key_file_get_integer(file, group, "type", &error);
  if (error)
    goto out_err;
  g_assert(type < NR_SITE_TYPE);
  init_nchip_type(chip, type,
		  geom[0], intervals[0], geomsizes[0],
		  geom[1], intervals[1], geomsizes[1]);

 out_err:
  if (geom[1])
    g_free(geom[1]);
  if (intervals[1])
    g_free(intervals[1]);
  if (geom[0])
    g_free(geom[0]);
  if (intervals[0])
    g_free(intervals[0]);
  if (error) {
    g_warning("Error treating group %s: %s",
	      group, error->message);
    g_error_free(error);
  }
  return;
}

static void
init_nchip(chip_descr_t *chip, GKeyFile *file) {
  /* Initialize area */
  iterate_over_groups(file, init_area_chip_type, chip);
}

/* exported alloc and destroy functions */
chip_descr_t *
get_chip(const gchar *dirname, const unsigned chipid) {
  chip_descr_t *chip = g_new0(chip_descr_t, 1);
  const gchar *chipname = chipfiles[chipid];
  GKeyFile *keyfile;
  GError *error = NULL;
  gchar *filename;
  int err;

  filename = g_build_filename(dirname, CHIP, chipname, "chip_control", NULL);
  err = read_keyfile(&keyfile,filename);
  g_free(filename);
  if (err)
    goto out_err;

#define DIM "DIMENTIONS"
  /* Get width and height */
  chip->width  = g_key_file_get_integer(keyfile, DIM, "WIDTH", &error);
  if (error)
    goto out_err_free_err_keyfile;
  chip->height = g_key_file_get_integer(keyfile, DIM, "HEIGHT", &error);
  if (error)
    goto out_err_free_err_keyfile;
  chip->awidth  = g_key_file_get_integer(keyfile, DIM, "CWIDTH", &error);
  if (error)
    goto out_err_free_err_keyfile;
  chip->aheight = g_key_file_get_integer(keyfile, DIM, "CHEIGHT", &error);
  if (error)
    goto out_err_free_err_keyfile;

  alloc_chip(chip);
  alloc_nchip(chip);
  g_key_file_free(keyfile);

  filename = g_build_filename(dirname, CHIP, chipname, "chip_data", NULL);
  err = read_keyfile(&keyfile,filename);
  g_free(filename);
  if (err)
    goto out_err;

  init_chip(chip, keyfile);
  init_nchip(chip, keyfile);
  g_key_file_free(keyfile);

  return chip;

 out_err_free_err_keyfile:
  g_warning("error reading chip description: %s", error->message);
  g_error_free(error);
  g_key_file_free(keyfile);
 out_err:
  free_chip(chip);
  free_nchip(chip);
  g_free(chip);
  return NULL;
}

void
release_chip(chip_descr_t *chip) {
  free_nchip(chip);
  free_chip(chip);
  g_free(chip);
}

/*
 * Utility function
 */

/*
 * This is the compact form of the site printing function.  We could
 * also do only one call with the "%0.0i" trick, and doing some tricky
 * initialization. This other solution is, however, a bit more
 * complicated, so we'll leave this as-is for now.
 */

typedef enum _site_print_type {
  PRINT_BOTH = 0,
  PRINT_X,
  PRINT_Y,
  PRINT_BOTH_INVERT,
} site_print_t;

#if defined(VIRTEX2) || defined(SPARTAN3)

static const site_print_t print_type[NR_SITE_TYPE] = {
  [TIOI] = PRINT_X,
  [BIOI] = PRINT_X,
  [LIOI] = PRINT_Y,
  [RIOI] = PRINT_Y,
  [RTERM] = PRINT_Y,
  [LTERM] = PRINT_Y,
  [TTERM] = PRINT_X,
  [BTERM] = PRINT_X,
  [TTERMBRAM] = PRINT_X,
  [BTERMBRAM] = PRINT_X,
  [TIOIBRAM] = PRINT_X,
  [BIOIBRAM] = PRINT_X,
};

static const char *print_str[NR_SITE_TYPE] = {
  [SITE_TYPE_NEUTRAL] = "GLOBALR%iC%i",
  [CLB] = "R%iC%i",
  [RTERM] = "RTERMR%i",
  [LTERM] = "LTERMR%i",
  [TTERM] = "TTERMC%i",
  [BTERM] = "BTERMC%i",
  [TIOI] = "TIOIC%i",
  [BIOI] = "BIOIC%i",
  [LIOI] = "LIOIR%i",
  [RIOI] = "RIOIR%i",
  [TTERMBRAM] = "TTERMBRAMC%i",
  [BTERMBRAM] = "BTERMBRAMC%i",
  [TIOIBRAM] = "TIOIBRAMC%i",
  [BIOIBRAM] = "BIOIBRAMC%i",
  [BRAM] = "BRAMR%iC%i",
};

/* Dangerous, but working for now */
static const char **sw_str = print_str;
static const site_print_t *sw_type = print_type;

#elif defined(VIRTEX4) || defined(VIRTEX5)

static const site_print_t print_type[NR_SITE_TYPE] = {
  [SITE_TYPE_NEUTRAL] = PRINT_BOTH_INVERT,
  [IOB] = PRINT_BOTH_INVERT,
  [CLB] = PRINT_BOTH_INVERT,
  [DSP48] = PRINT_BOTH_INVERT,
  [GCLKC] = PRINT_BOTH_INVERT,
  [BRAM] = PRINT_BOTH_INVERT,
};

static const char *print_str[NR_SITE_TYPE] = {
  [SITE_TYPE_NEUTRAL] = "NEUTRAL_X%iY%i",
  [IOB] = "IOB_X%iY%i",
  [CLB] = "CLB_X%iY%i",
  [DSP48] = "DSP48_X%iY%i",
  [GCLKC] = "GCLKC_X%iY%i",
  [BRAM] = "BRAM_X%iY%i",
};

static const site_print_t sw_type[NR_SWITCH_TYPE] = {
  [SW_NONE] = PRINT_BOTH_INVERT,
  [SW_INT] = PRINT_BOTH_INVERT,
  [SW_CLB] = PRINT_BOTH_INVERT,
};

static const char *sw_str[NR_SWITCH_TYPE] = {
  [SW_NONE] = "NONE_X%iY%i",
  [SW_INT] = "INT_X%iY%i",
  [SW_CLB] = "CLB_X%iY%i",
};

#endif

int
snprint_csite(gchar *buf, size_t bufs,
	      const csite_descr_t *site,
	      const unsigned gx, const unsigned gy) {
  const char *str = print_str[site->type];
  const site_print_t strtype = print_type[site->type];
  const guint x = site->type_coord.x + 1;
  const guint y = site->type_coord.y + 1;

  if (!str)
    str = print_str[SITE_TYPE_NEUTRAL];

  switch (strtype) {
    case PRINT_BOTH:
      return snprintf(buf, bufs, str, y, x);
    case PRINT_X:
      return snprintf(buf, bufs, str, x);
    case PRINT_Y:
      return snprintf(buf, bufs, str, y);
    /* V4-style site naming needs global coordinates */
    case PRINT_BOTH_INVERT:
      return snprintf(buf, bufs, str, gx, gy);
  }
  g_assert_not_reached();
  return -1;
}

/* Print the slice name. At some point, the site should be embedded into
   the site maybe ? XXX *site redundant with chip */
int
snprint_slice(gchar *buf, size_t buf_len, const chip_descr_t *chip,
	      const csite_descr_t *site, const slice_index_t slice) {
  /* Only works for slices for now */
  const char *str = "SLICE_X%iY%i";
  const site_print_t strtype = PRINT_BOTH;
  const guint x = site->type_coord.x * 2 + BITAT(slice,1);
  /* Invert the y coordinate */
  const unsigned clb_h = (chip->height - 4);
  const guint y = 2 * (clb_h - 1 - site->type_coord.y) + BITAT(slice,0);

  switch (strtype) {
  case PRINT_BOTH:
    return snprintf(buf, buf_len, str, x, y);
  default:
    return 0;
  }
}

int
parse_slice_simple(const gchar *buf, slice_index_t* idx) {
  const char *str = "SLICE_X%uY%u";
  unsigned x, y;
  int assoc;

  assoc = sscanf(buf, str, &x, &y);
  if (assoc != 2)
    return -1;

  *idx = (BITAT(x, 0) << 1) + BITAT(y, 0);
  return 0;
}

int
snprint_switch(gchar *buf, size_t bufs,
	       const chip_descr_t *chip,
	       const site_ref_t site_ref) {
  const csite_descr_t *site = get_site(chip, site_ref);
  const switch_type_t switch_ref = sw_of_type(site->type);
  const char *str = sw_str[switch_ref];
  const site_print_t strtype = sw_type[switch_ref];

  const guint x = site->type_coord.x + 1;
  const guint y = site->type_coord.y + 1;

  if (!str)
    str = sw_str[SW_NONE];

  switch (strtype) {
  case PRINT_BOTH:
    return snprintf(buf, bufs, str, y, x);
  case PRINT_X:
    return snprintf(buf, bufs, str, x);
  case PRINT_Y:
    return snprintf(buf, bufs, str, y);
    /* V4-style site naming needs global coordinates */
  case PRINT_BOTH_INVERT: {
    unsigned width = chip->width;
    unsigned gx = site_ref % width,
      gy = site_ref / width;
    return snprintf(buf, bufs, str, gx, gy);
  }
  }
  g_assert_not_reached();
  return -1;
}

/*
 * Site to csite translation
 */

int parse_site_simple(const chip_descr_t *chip,
		      site_ref_t* sref,
		      const gchar *lookup) {
  chip_descr_t *mchip = (void *) chip;
  csite_descr_t *site = g_datalist_get_data(&mchip->lookup, lookup);

  if (site == NULL)
    return -1;

  *sref = get_site_ref(chip, site);
  return 0;
}

int parse_site_complex(const chip_descr_t *chip,
		       site_ref_t* sref,
		       const gchar *lookup) {
  unsigned x = 0, y = 0;
  site_type_t i;

  (void) chip; (void) sref;

  for (i = SITE_TYPE_NEUTRAL; i < NR_SITE_TYPE; i++) {
    int num = 0, exp = 1;
    const char *str = print_str[i];
    if (!str)
      continue;

    switch (print_type[i]) {
    case PRINT_BOTH:
      num = sscanf(lookup, str, &y, &x);
      exp = 2;
      break;
    case PRINT_X:
      num = sscanf(lookup, str, &x);
      break;
    case PRINT_Y:
      num = sscanf(lookup, str, &y);
      break;
    default:
      num = -1;
    }

    if (num == exp)
      break;
  }

  if (i == NR_SITE_TYPE)
    return -1;

  return -1;
  //  return find_site(x-1, y-1, i, sref);
}

static void
print_iterator(unsigned x, unsigned y,
	       csite_descr_t *site, gpointer dat) {
  gchar name[MAX_SITE_NLEN];
  snprint_csite(name, ARRAY_SIZE(name), site, x, y);
  (void) dat;
  g_print("global site (%i,%i) is %s\n", x, y, name);
}

void
print_chip(chip_descr_t *chip) {
  iterate_over_sites(chip, print_iterator, NULL);
}
