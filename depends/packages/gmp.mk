package=gmp
$(package)_version=6.2.1
$(package)_download_path=https://gmplib.org/download/$(package)/
$(package)_file_name=$(package)-$($(package)_version).tar.xz
$(package)_sha256_hash=fd4829912cddd12f84181c3451cc752be224643e87fac497b69edddadc49b4f2
$(package)_patches=arm64-avoid-x18.patch

define $(package)_preprocess_cmds
  patch -p1 < $($(package)_patch_dir)/arm64-avoid-x18.patch
endef

define $(package)_set_vars
  $(package)_config_opts=--enable-cxx
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
