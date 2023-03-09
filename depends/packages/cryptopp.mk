package=cryptopp
$(package)_version=8_6_0
$(package)_download_path=https://github.com/weidai11/cryptopp/releases/download/CRYPTOPP_$($(package)_version)/
$(package)_file_name=cryptopp860.zip
$(package)_sha256_hash=20aa413957d9c8ae353ee2f7747bd7ac392f933c60a53e3fd1e41cadbc48d193
$(package)_dependencies=

define $(package)_set_vars
  $(package)_config_opts_darwin = CXXFLAGS=-mmacosx-version-min=11.0
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
	echo "$$($(1)_sha256_hash)  $$($(1)_source)" > $$($(1)_extract_dir)/.$$($(1)_file_name).hash && \
	$(build_SHA256SUM) -c $$($(1)_extract_dir)/.$$($(1)_file_name).hash && \
	unzip $$($(1)_source)
endef

define $(package)_build_cmds
  $(MAKE) $($(package)_config_opts)
endef

define $(package)_stage_cmds
  $(MAKE) PREFIX=$($(package)_staging_prefix_dir) $($(package)_staging_dir) install
endef
