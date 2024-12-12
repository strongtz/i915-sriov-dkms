#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

KSYMTAB_FUNC(i915_read_mch_val, "_gpl", "");
KSYMTAB_FUNC(i915_gpu_raise, "_gpl", "");
KSYMTAB_FUNC(i915_gpu_lower, "_gpl", "");
KSYMTAB_FUNC(i915_gpu_busy, "_gpl", "");
KSYMTAB_FUNC(i915_gpu_turbo_disable, "_gpl", "");

SYMBOL_CRC(i915_read_mch_val, 0x500858b9, "_gpl");
SYMBOL_CRC(i915_gpu_raise, 0x08a7896d, "_gpl");
SYMBOL_CRC(i915_gpu_lower, 0x402468e9, "_gpl");
SYMBOL_CRC(i915_gpu_busy, 0x05876c69, "_gpl");
SYMBOL_CRC(i915_gpu_turbo_disable, 0xe7237b0b, "_gpl");

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x7381287f, "trace_handle_return" },
	{ 0xa415f3fd, "cpu_sibling_map" },
	{ 0x28c63cf3, "kobject_add" },
	{ 0x2e3bcce2, "wait_for_completion_interruptible" },
	{ 0x8fa8aca1, "__cpuhp_state_remove_instance" },
	{ 0xf0e8618f, "pci_enable_sriov" },
	{ 0x3dad9978, "cancel_delayed_work" },
	{ 0x7a45377b, "acpi_video_unregister" },
	{ 0xf98c35ed, "__irq_alloc_descs" },
	{ 0xb19b445, "ioread8" },
	{ 0xf1b5340a, "drm_mode_vrefresh" },
	{ 0xffeedf6a, "delayed_work_timer_fn" },
	{ 0x296695f, "refcount_warn_saturate" },
	{ 0xfe12bcb9, "drm_dsc_compute_rc_parameters" },
	{ 0xe643f98b, "simple_attr_open" },
	{ 0x74c134b9, "__sw_hweight32" },
	{ 0x1dacf936, "pci_disable_msi" },
	{ 0x75d7bd7e, "pci_enable_device" },
	{ 0x65929cae, "ns_to_timespec64" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0x59f27ed7, "drm_dp_pcon_enc_is_dsc_1_2" },
	{ 0xf474c21c, "bitmap_print_to_pagebuf" },
	{ 0x27bd748e, "drm_dp_cec_register_connector" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x6505edb3, "folio_alloc" },
	{ 0x28e112a2, "drm_fb_helper_restore_fbdev_mode_unlocked" },
	{ 0x3c3fce39, "__local_bh_enable_ip" },
	{ 0xc1198662, "__warn_flushing_systemwide_wq" },
	{ 0x37110088, "remove_wait_queue" },
	{ 0x17de3d5, "nr_cpu_ids" },
	{ 0x4403a9c3, "drm_mode_get_hv_timing" },
	{ 0x21ea5251, "__bitmap_weight" },
	{ 0xfb1a7a5a, "drm_dp_downstream_rgb_to_ycbcr_conversion" },
	{ 0x148653, "vsnprintf" },
	{ 0xb8f11603, "idr_alloc" },
	{ 0x122c3a7e, "_printk" },
	{ 0x92aacc18, "pci_resize_resource" },
	{ 0xb212f969, "drm_plane_create_color_properties" },
	{ 0xc512626a, "__supported_pte_mask" },
	{ 0x1920c9ac, "mipi_dsi_compression_mode" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xcd86cca3, "sysfs_remove_groups" },
	{ 0x4129f5ee, "kernel_fpu_begin_mask" },
	{ 0xc3e52d15, "drm_dp_dual_mode_detect" },
	{ 0x2801a22f, "proc_dointvec_minmax" },
	{ 0xafb864c1, "refcount_dec_and_lock_irqsave" },
	{ 0xc1726dee, "drm_hdcp_update_content_protection" },
	{ 0x78e20f5a, "drm_dp_mst_detect_port" },
	{ 0x2f2c95c4, "flush_work" },
	{ 0x5e06bc5c, "refcount_dec_and_lock" },
	{ 0x44350fea, "folio_mark_accessed" },
	{ 0xdf521442, "_find_next_zero_bit" },
	{ 0x1996c36c, "simple_attr_release" },
	{ 0x593984a9, "drm_get_edid_switcheroo" },
	{ 0xd0654aba, "woken_wake_function" },
	{ 0x69353664, "__drm_debug" },
	{ 0x25daad93, "__drm_mm_interval_first" },
	{ 0xafd744c6, "__x86_indirect_thunk_rbp" },
	{ 0x2469810f, "__rcu_read_unlock" },
	{ 0x236a77a2, "dma_resv_add_fence" },
	{ 0xb0978ca3, "drm_dp_read_dpcd_caps" },
	{ 0x513072fe, "__drm_puts_seq_file" },
	{ 0x726103f2, "drm_encoder_init" },
	{ 0xc4676530, "debugfs_create_file" },
	{ 0xabb9deaa, "drm_atomic_get_mst_topology_state" },
	{ 0xfb6eedf9, "power_group_name" },
	{ 0xe8a034df, "drm_dev_exit" },
	{ 0x9466e03e, "__pm_runtime_use_autosuspend" },
	{ 0x9e9fdd9d, "memunmap" },
	{ 0xfdd4216d, "pcibios_align_resource" },
	{ 0xe9f7149c, "zlib_deflate_workspacesize" },
	{ 0xf9a482f9, "msleep" },
	{ 0xb8e9ea8b, "ttm_device_init" },
	{ 0x38aa1397, "gpiod_add_lookup_table" },
	{ 0x2183c08c, "drm_mm_scan_add_block" },
	{ 0x63a477fb, "drm_dp_downstream_min_tmds_clock" },
	{ 0xed7b247c, "dma_set_coherent_mask" },
	{ 0x3da171f9, "pci_mem_start" },
	{ 0xf74bb274, "mod_delayed_work_on" },
	{ 0x397c3d27, "drm_edid_free" },
	{ 0xf0163210, "__mmap_lock_do_trace_released" },
	{ 0xb02343ef, "debugfs_attr_write" },
	{ 0x7da16899, "sysfs_remove_link" },
	{ 0x4c9d28b0, "phys_base" },
	{ 0x648d953b, "drm_dsc_dp_pps_header_init" },
	{ 0xccf7fa29, "pci_write_config_dword" },
	{ 0x597ec2fa, "drm_dp_pcon_is_frl_ready" },
	{ 0x8d8f91db, "ttm_device_fini" },
	{ 0x5f51ae9e, "drm_mode_create_tv_properties_legacy" },
	{ 0xf8e7c07e, "simple_open" },
	{ 0xb2fcb56d, "queue_delayed_work_on" },
	{ 0xf906bdd1, "pm_runtime_no_callbacks" },
	{ 0x589091f8, "pci_write_config_word" },
	{ 0xb9422606, "drm_edid_connector_update" },
	{ 0x8d522714, "__rcu_read_lock" },
	{ 0x141f38bf, "ktime_get_raw_fast_ns" },
	{ 0xf7900cf0, "drm_dp_check_act_status" },
	{ 0xbcb36fe4, "hugetlb_optimize_vmemmap_key" },
	{ 0xec3db6e3, "drm_fb_helper_blank" },
	{ 0x670ecece, "__x86_indirect_thunk_rbx" },
	{ 0x646d3d12, "__srcu_read_unlock" },
	{ 0x1ba94cd9, "pci_get_domain_bus_and_slot" },
	{ 0x6e30ba8e, "drm_rect_rotate_inv" },
	{ 0x167c5967, "print_hex_dump" },
	{ 0x8b9b70b4, "drm_fb_helper_set_par" },
	{ 0x9e593e23, "_dev_printk" },
	{ 0xe926e158, "drm_edp_backlight_init" },
	{ 0xa065e463, "mipi_dsi_attach" },
	{ 0x3e7d7039, "kernel_param_lock" },
	{ 0x7682ba4e, "__copy_overflow" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x5871bbd, "drm_plane_create_zpos_immutable_property" },
	{ 0x6a5cb5ee, "__get_free_pages" },
	{ 0xc4f0da12, "ktime_get_with_offset" },
	{ 0x7ac1254b, "local_clock" },
	{ 0x97fa317c, "__cpuhp_setup_state" },
	{ 0x63c8a059, "drm_fb_helper_hotplug_event" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x20361e3c, "debugfs_remove" },
	{ 0x9ad8514d, "drm_framebuffer_remove" },
	{ 0xb02892d8, "pci_iomap_range" },
	{ 0x5be8b97b, "drm_mode_create" },
	{ 0x86b6f0cf, "sg_alloc_table_from_pages_segment" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0xb197531a, "ttm_bo_vm_fault_reserved" },
	{ 0x50d68377, "arch_phys_wc_del" },
	{ 0xb7c69a63, "unregister_vmap_purge_notifier" },
	{ 0x36624bd0, "drm_connector_attach_content_protection_property" },
	{ 0x81188c30, "match_string" },
	{ 0xc2f732a9, "drm_dp_mst_update_slots" },
	{ 0xb4032484, "drm_mm_insert_node_in_range" },
	{ 0x9a59bb31, "drm_scdc_set_high_tmds_clock_ratio" },
	{ 0x2fd65279, "drm_connector_list_iter_next" },
	{ 0xaeb4d00f, "drm_atomic_helper_damage_merged" },
	{ 0x3b6c41ea, "kstrtouint" },
	{ 0x2d3385d3, "system_wq" },
	{ 0x81d57e73, "pci_release_resource" },
	{ 0xe783e261, "sysfs_emit" },
	{ 0xb9bed10a, "drm_dev_register" },
	{ 0xe66a589e, "ttm_pool_free" },
	{ 0xb2b89cc, "drm_hdmi_avi_infoframe_from_display_mode" },
	{ 0x4cf7465, "drm_modeset_drop_locks" },
	{ 0x8c170ff0, "drm_dp_cec_set_edid" },
	{ 0xad73041f, "autoremove_wake_function" },
	{ 0x2d1a036, "pci_assign_unassigned_bus_resources" },
	{ 0x75d0deb9, "nsecs_to_jiffies64" },
	{ 0x64709236, "drm_gem_private_object_init" },
	{ 0x654bf746, "__free_pages" },
	{ 0x192ea14f, "__SCT__tp_func_dma_fence_signaled" },
	{ 0x5a86f411, "drm_dp_phy_name" },
	{ 0x56a663e9, "drm_dp_dsc_sink_line_buf_depth" },
	{ 0x31e9355f, "drm_dp_get_vc_payload_bw" },
	{ 0x8b017391, "drm_framebuffer_cleanup" },
	{ 0x8810754a, "_find_first_bit" },
	{ 0x53a1e8d9, "_find_next_bit" },
	{ 0xd5b1a2a7, "pm_runtime_set_autosuspend_delay" },
	{ 0x26ed2186, "register_vmap_purge_notifier" },
	{ 0x5786dc05, "drm_atomic_helper_set_config" },
	{ 0x9985df60, "drm_dp_read_downstream_info" },
	{ 0xde60e094, "drm_dp_read_sink_count_cap" },
	{ 0x1a8787cb, "pci_sriov_set_totalvfs" },
	{ 0x18c1f1b0, "drm_plane_from_index" },
	{ 0x180dcbdf, "drm_dp_cec_irq" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0xd289a8f, "dma_resv_get_singleton" },
	{ 0x38690d99, "drm_detect_hdmi_monitor" },
	{ 0xcef1030a, "anon_inode_getfd" },
	{ 0x44f0ad9, "get_random_u16" },
	{ 0x48c1b5ef, "drm_crtc_vblank_put" },
	{ 0x55eb38da, "drm_format_info" },
	{ 0xbf3982a5, "__i2c_transfer" },
	{ 0x6cdbb9dd, "pci_bus_write_config_byte" },
	{ 0x909b3e6e, "pv_ops" },
	{ 0x64127b67, "bitmap_find_next_zero_area_off" },
	{ 0x94961283, "vunmap" },
	{ 0xdd4d55b6, "_raw_read_unlock" },
	{ 0x32e20b61, "drm_edp_backlight_set_level" },
	{ 0xde49288c, "drm_fb_helper_init" },
	{ 0x6cada444, "__auxiliary_device_add" },
	{ 0x35362b4c, "drm_mode_object_find" },
	{ 0x4302d0eb, "free_pages" },
	{ 0xb88e1d26, "__drm_atomic_helper_crtc_destroy_state" },
	{ 0xad9901ae, "bit_waitqueue" },
	{ 0x4d924f20, "memremap" },
	{ 0xe43cd3e0, "drmm_mode_config_init" },
	{ 0x6091797f, "synchronize_rcu" },
	{ 0x1e3c343e, "clear_page_dirty_for_io" },
	{ 0xe0112fc4, "__x86_indirect_thunk_r9" },
	{ 0xf0aa5d78, "drm_dp_pcon_hdmi_link_mode" },
	{ 0x4161a911, "drm_compat_ioctl" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0xf90a1e85, "__x86_indirect_thunk_r8" },
	{ 0xc948c45f, "__tracepoint_dma_fence_signaled" },
	{ 0x3107eff0, "drm_mode_destroy" },
	{ 0x2dc7f612, "drm_crtc_init_with_planes" },
	{ 0xe9fcee7, "check_move_unevictable_folios" },
	{ 0x14b607c, "drm_read" },
	{ 0x4603c416, "drm_dp_pcon_dsc_bpp_incr" },
	{ 0x97b1174f, "pci_enable_msi" },
	{ 0xf3808cb1, "get_state_synchronize_rcu" },
	{ 0xd2eb8197, "drm_property_create_range" },
	{ 0x9b7c0b43, "ttm_bo_move_to_lru_tail" },
	{ 0xa78af5f3, "ioread32" },
	{ 0x69acdf38, "memcpy" },
	{ 0xb02781e6, "_dev_notice" },
	{ 0x1a71d30c, "ttm_move_memcpy" },
	{ 0xf7ef9a79, "iosf_mbi_punit_release" },
	{ 0xe2e21c25, "drm_kms_helper_poll_disable" },
	{ 0x27107886, "gpiod_get" },
	{ 0x9ccaa1a1, "seq_write" },
	{ 0x4d9b652b, "rb_erase" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0xc1514a3b, "free_irq" },
	{ 0x5aa0a11d, "dma_buf_detach" },
	{ 0xe40976c0, "pnp_range_reserved" },
	{ 0x61c6e7af, "dma_buf_map_attachment" },
	{ 0xe02c9c92, "__xa_erase" },
	{ 0xfd93ee35, "ioremap_wc" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x550ce709, "pat_enabled" },
	{ 0x2d09d74a, "drm_plane_cleanup" },
	{ 0xb212dc3e, "drm_edid_dup" },
	{ 0xf5dd14b9, "pm_runtime_allow" },
	{ 0x754d539c, "strlen" },
	{ 0xb5a702e9, "pci_bus_resource_n" },
	{ 0x9baa4b95, "i2c_transfer" },
	{ 0x134deb5f, "kern_unmount" },
	{ 0x6a4df8c5, "drm_dp_128b132b_eq_interlane_align_done" },
	{ 0x80fd3091, "drm_gem_fb_get_obj" },
	{ 0xdd9b5be, "drm_hdmi_avi_infoframe_content_type" },
	{ 0x103cd51d, "drm_modeset_lock_all" },
	{ 0xad96d7f1, "drm_dp_128b132b_read_aux_rd_interval" },
	{ 0xb9e7429c, "memcpy_toio" },
	{ 0x6aacee47, "drm_dp_128b132b_link_training_failed" },
	{ 0x75c6dbd9, "drm_dp_read_lttpr_common_caps" },
	{ 0xeb05d7ce, "drm_syncobj_replace_fence" },
	{ 0x1d19f77b, "physical_mask" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xb2f74fb6, "intel_gmch_remove" },
	{ 0xeb6eb87, "add_taint" },
	{ 0xb695b47d, "kobject_put" },
	{ 0x944375db, "_totalram_pages" },
	{ 0x19668f0c, "drm_connector_set_vrr_capable_property" },
	{ 0x21d80c17, "drm_crtc_vblank_waitqueue" },
	{ 0xffb7c514, "ida_free" },
	{ 0x2ce231f0, "drm_syncobj_find" },
	{ 0xb9478d90, "hdmi_drm_infoframe_unpack_only" },
	{ 0xfc45b99b, "pci_num_vf" },
	{ 0xe6a5e4aa, "drm_framebuffer_plane_height" },
	{ 0x4b1d7de2, "simple_attr_read" },
	{ 0x58f4959a, "single_release" },
	{ 0x28afbb08, "cpu_latency_qos_add_request" },
	{ 0x51a6e343, "drm_crtc_vblank_get" },
	{ 0x78d27962, "boot_cpu_data" },
	{ 0x68a12ab8, "rep_movs_alternative" },
	{ 0x49727a88, "drm_fb_helper_damage_range" },
	{ 0xdb51e361, "ttm_resource_manager_evict_all" },
	{ 0xedc03953, "iounmap" },
	{ 0x820ac5c0, "drm_vma_node_allow_once" },
	{ 0x3c906c73, "drm_hdmi_infoframe_set_hdr_metadata" },
	{ 0xc0598da0, "device_create_bin_file" },
	{ 0x56ee0fdb, "drm_mode_config_reset" },
	{ 0x92540fbf, "finish_wait" },
	{ 0xd97e2b03, "drm_av_sync_delay" },
	{ 0x67d16070, "devm_hwmon_device_register_with_info" },
	{ 0xcc5005fe, "msleep_interruptible" },
	{ 0xe8dd3160, "drm_kms_helper_poll_fini" },
	{ 0x2d0751c7, "i2c_put_adapter" },
	{ 0x521ad6d0, "drm_puts" },
	{ 0x68fc4ed8, "drm_kms_helper_hotplug_event" },
	{ 0xde91431, "pwm_get" },
	{ 0x6e577ba5, "drm_mode_create_hdmi_colorspace_property" },
	{ 0x85df9b6c, "strsep" },
	{ 0xcdc598, "drm_add_edid_modes" },
	{ 0x9264d7a, "drm_fb_helper_debug_enter" },
	{ 0x6eebc737, "drm_release_noglobal" },
	{ 0x266a4b08, "tasklet_unlock" },
	{ 0xd955afa6, "hrtimer_start_range_ns" },
	{ 0x5b3e282f, "xa_store" },
	{ 0x4f885fda, "kobj_sysfs_ops" },
	{ 0x3cf89fe4, "ttm_bo_unmap_virtual" },
	{ 0xbb5dd4a, "dma_fence_chain_find_seqno" },
	{ 0x689067dd, "dma_fence_chain_ops" },
	{ 0x14605535, "dma_fence_context_alloc" },
	{ 0xfd43d459, "latent_entropy" },
	{ 0xad5f0017, "perf_trace_buf_alloc" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x797c1bfb, "mipi_dsi_dcs_read" },
	{ 0xfe5573ae, "vga_switcheroo_client_probe_defer" },
	{ 0x6f79888b, "pci_disable_sriov" },
	{ 0xd3443bc2, "ttm_bo_vm_dummy_page" },
	{ 0xdd3c6afa, "drm_mode_is_420_also" },
	{ 0x392a838b, "drm_dp_downstream_max_dotclock" },
	{ 0x745a981, "xa_erase" },
	{ 0xb17cc11b, "drm_dp_mst_topology_mgr_set_mst" },
	{ 0x1ba4e189, "drm_atomic_state_init" },
	{ 0xc94e7b43, "drm_atomic_set_fb_for_plane" },
	{ 0xedcf81ce, "drm_dp_channel_eq_ok" },
	{ 0xd9235101, "sysfs_create_link" },
	{ 0x19aec5ac, "__folio_batch_release" },
	{ 0x1b63685b, "drm_is_current_master" },
	{ 0x7ceaf0d5, "generic_handle_irq" },
	{ 0x151f4898, "schedule_timeout_uninterruptible" },
	{ 0x3c12dfe, "cancel_work_sync" },
	{ 0x1d7da16d, "drm_dp_downstream_debug" },
	{ 0x1e28f949, "drm_atomic_helper_setup_commit" },
	{ 0xef2df267, "drm_modeset_lock_all_ctx" },
	{ 0x449ad0a7, "memcmp" },
	{ 0xa82d01a1, "drm_aperture_remove_conflicting_pci_framebuffers" },
	{ 0xe7d81ada, "pwm_apply_state" },
	{ 0x4b2a0cf5, "relay_file_operations" },
	{ 0x96f724a9, "drm_fb_helper_setcmap" },
	{ 0xe8cf02a4, "drm_atomic_helper_update_plane" },
	{ 0xdab20fdf, "dma_resv_iter_first" },
	{ 0x999e8297, "vfree" },
	{ 0x369158fa, "drm_hdmi_avi_infoframe_colorimetry" },
	{ 0x2db60714, "drm_dp_mst_connector_late_register" },
	{ 0x8427cc7b, "_raw_spin_lock_irq" },
	{ 0x90497fa2, "drm_crtc_vblank_reset" },
	{ 0x163ee5d8, "drm_atomic_helper_disable_all" },
	{ 0x3221df67, "__bitmap_subset" },
	{ 0xdd18a993, "acpi_check_dsm" },
	{ 0xa64cd1a3, "drm_atomic_helper_wait_for_dependencies" },
	{ 0x64e9cca0, "drm_edid_connector_add_modes" },
	{ 0xb5e73116, "flush_delayed_work" },
	{ 0xeafa06b2, "dma_resv_init" },
	{ 0x7a8163d, "drm_crtc_handle_vblank" },
	{ 0xe2964344, "__wake_up" },
	{ 0x6f5c3913, "anon_inode_getfile" },
	{ 0x8809de8, "drm_mode_create_aspect_ratio_property" },
	{ 0xf8f61ebc, "wake_up_var" },
	{ 0xe2c17b5d, "__SCT__might_resched" },
	{ 0xe26cf4d6, "dma_resv_reserve_fences" },
	{ 0x7023bea8, "unregister_acpi_notifier" },
	{ 0x185af984, "drm_scdc_read" },
	{ 0x4449543a, "drm_get_edid" },
	{ 0xa70f8bcf, "pinctrl_lookup_state" },
	{ 0xc42c27da, "bus_register_notifier" },
	{ 0xbcaffa2e, "drm_mode_duplicate" },
	{ 0x4489a5e9, "drm_edid_raw" },
	{ 0x3607a28f, "drm_plane_create_rotation_property" },
	{ 0x588962fa, "drm_property_blob_put" },
	{ 0x8298fb1c, "drm_connector_attach_encoder" },
	{ 0xe1a515bc, "platform_device_unregister" },
	{ 0x7012caab, "ttm_resource_init" },
	{ 0xbd62c275, "drm_connector_update_privacy_screen" },
	{ 0x8826c13b, "acpi_video_register" },
	{ 0xfda9a3f1, "intel_gmch_enable_gtt" },
	{ 0x65c32d2e, "is_acpi_device_node" },
	{ 0xc8827b75, "sysctl_vals" },
	{ 0xd239f0cc, "drm_hdmi_vendor_infoframe_from_display_mode" },
	{ 0xa313296c, "drm_fb_helper_unregister_info" },
	{ 0x22e9a579, "pci_dev_put" },
	{ 0xe3ff2c41, "get_random_u64" },
	{ 0x60bbb124, "dma_fence_signal_locked" },
	{ 0x7dd3f84f, "drm_atomic_set_mode_for_crtc" },
	{ 0x9ce050be, "drm_mode_copy" },
	{ 0x47183b9a, "pci_read_config_word" },
	{ 0x4122f45, "drm_atomic_set_crtc_for_connector" },
	{ 0xa2e2191f, "drm_crtc_from_index" },
	{ 0x338e80e9, "hrtimer_try_to_cancel" },
	{ 0x4831da6e, "drm_vma_offset_remove" },
	{ 0x6e9dd606, "__symbol_put" },
	{ 0xb5688697, "kmem_cache_free" },
	{ 0xfbe215e4, "sg_next" },
	{ 0xa3188cc1, "ww_mutex_unlock" },
	{ 0x40897c5d, "sysfs_create_group" },
	{ 0xc2e9e99b, "pci_set_master" },
	{ 0xa0fbac79, "wake_up_bit" },
	{ 0xc076a989, "drm_dp_set_subconnector_property" },
	{ 0x66cca4f9, "__x86_indirect_thunk_rcx" },
	{ 0xbda02e6, "drm_dp_mst_atomic_check" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0x7ae0aae6, "drm_atomic_helper_update_legacy_modeset_state" },
	{ 0xa8f45199, "drm_dp_mst_topology_mgr_destroy" },
	{ 0x2d9e9583, "drm_buddy_print" },
	{ 0x9d2ab8ac, "__tasklet_schedule" },
	{ 0x29ce0107, "ttm_bo_vm_reserve" },
	{ 0xaa7dd5d, "zap_vma_ptes" },
	{ 0x29332499, "__x86_indirect_thunk_rsi" },
	{ 0x9809decd, "drm_modeset_unlock_all" },
	{ 0xc84a60, "__drm_atomic_helper_plane_duplicate_state" },
	{ 0x2fa94ef2, "drm_dp_downstream_444_to_420_conversion" },
	{ 0x2529d382, "acpi_dev_get_resources" },
	{ 0xca9360b5, "rb_next" },
	{ 0x5a773cfa, "perf_pmu_migrate_context" },
	{ 0xd0a3c05c, "__tracepoint_mmap_lock_released" },
	{ 0x41402e, "drm_atomic_helper_connector_destroy_state" },
	{ 0x531cf724, "pwm_put" },
	{ 0xabd45848, "stop_machine" },
	{ 0x43fe2e69, "drm_property_create_enum" },
	{ 0x277fed4b, "sync_file_create" },
	{ 0x4a35d30d, "drm_mode_set_name" },
	{ 0x96848186, "scnprintf" },
	{ 0xffe0f3ff, "drm_fb_helper_set_suspend" },
	{ 0x2a55b87b, "generic_file_llseek" },
	{ 0xd370d1e7, "dma_map_sg_attrs" },
	{ 0xc6cbbc89, "capable" },
	{ 0x871ab41a, "drm_rect_intersect" },
	{ 0xa38602cd, "drain_workqueue" },
	{ 0x40ddba1c, "drm_fb_helper_fini" },
	{ 0xcbd4898c, "fortify_panic" },
	{ 0xc9992577, "drm_syncobj_create" },
	{ 0x57f537fe, "pci_disable_device" },
	{ 0xfcb1ca12, "kmem_cache_destroy" },
	{ 0x698307b0, "bpf_trace_run5" },
	{ 0x7682ffc2, "drm_atomic_get_crtc_state" },
	{ 0x72b3598e, "vga_get" },
	{ 0xffb5a92d, "drm_crtc_wait_one_vblank" },
	{ 0x628c0f8f, "dma_fence_add_callback" },
	{ 0x4e6e4b41, "radix_tree_delete" },
	{ 0xaacf03c1, "dma_fence_signal" },
	{ 0x6e813919, "drm_fb_helper_damage_area" },
	{ 0x2d393f48, "intel_soc_pmic_exec_mipi_pmic_seq_element" },
	{ 0x152c3e4c, "__mmap_lock_do_trace_acquire_returned" },
	{ 0xde976b71, "bpf_trace_run2" },
	{ 0x656e4a6e, "snprintf" },
	{ 0x24cf437a, "drm_vma_node_is_allowed" },
	{ 0x67b3c9cb, "bpf_trace_run1" },
	{ 0x597f50ec, "ttm_bo_wait_ctx" },
	{ 0x70daa11e, "dma_fence_remove_callback" },
	{ 0x7053fa72, "drm_dp_get_pcon_max_frl_bw" },
	{ 0xd06c2d24, "dma_resv_iter_next" },
	{ 0x48d27375, "__bitmap_intersects" },
	{ 0xd9c8bbec, "drm_modeset_lock" },
	{ 0xbd011b81, "dma_map_sgtable" },
	{ 0x70c29d2e, "bpf_trace_run3" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0x5439ab29, "bpf_trace_run4" },
	{ 0x1d24c881, "___ratelimit" },
	{ 0x38b26499, "drm_prime_gem_destroy" },
	{ 0x8d701329, "drm_dp_clock_recovery_ok" },
	{ 0x2946fd01, "platform_device_register_full" },
	{ 0x7d628444, "memcpy_fromio" },
	{ 0xf2eda7cc, "debugfs_create_dir" },
	{ 0x37befc70, "jiffies_to_msecs" },
	{ 0x9166fc03, "__flush_workqueue" },
	{ 0x9ad7a582, "iosf_mbi_assert_punit_acquired" },
	{ 0xed07b1c2, "component_add_typed" },
	{ 0x587f22d7, "devmap_managed_key" },
	{ 0xcc328a5c, "reservation_ww_class" },
	{ 0x7aa1756e, "kvfree" },
	{ 0x79c00fa2, "drm_edid_alloc" },
	{ 0xf30965ac, "iosf_mbi_register_pmic_bus_access_notifier" },
	{ 0xbabe01ce, "mipi_dsi_dcs_write_buffer" },
	{ 0xe5b2efe0, "__folio_put" },
	{ 0x7642f2cb, "drm_kms_helper_connector_hotplug_event" },
	{ 0xebbe357, "drm_dp_read_channel_eq_delay" },
	{ 0x8d9651a2, "trace_raw_output_prep" },
	{ 0x32fa4de4, "vm_mmap" },
	{ 0x599fb41c, "kvmalloc_node" },
	{ 0xac71b04d, "__devm_request_region" },
	{ 0x9380edfa, "fwnode_handle_put" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x3928efe9, "__per_cpu_offset" },
	{ 0x2808840, "drm_dp_mst_atomic_setup_commit" },
	{ 0x9b95c885, "drm_mode_match" },
	{ 0x7a0d8df9, "get_fs_type" },
	{ 0xcb89e809, "fb_io_write" },
	{ 0xf08b5da4, "drm_crtc_vblank_on" },
	{ 0xc54e1706, "unregister_sysctl_table" },
	{ 0x93d6dd8c, "complete_all" },
	{ 0xfee9906e, "shmem_file_setup" },
	{ 0x31cf8565, "unpin_user_pages" },
	{ 0xf0ef014f, "synchronize_srcu_expedited" },
	{ 0x9714e0bb, "ktime_get_raw" },
	{ 0x26815dbc, "drm_dp_link_rate_to_bw_code" },
	{ 0x9e7d6bd0, "__udelay" },
	{ 0x77bc13a0, "strim" },
	{ 0x9aadac4f, "__drm_atomic_helper_crtc_duplicate_state" },
	{ 0x64d11af1, "dma_fence_wait_timeout" },
	{ 0x650ada13, "folio_mark_dirty" },
	{ 0x51157910, "drm_dp_read_lttpr_phy_caps" },
	{ 0xfbc4f89e, "io_schedule_timeout" },
	{ 0x8c6edbbd, "vma_set_file" },
	{ 0xbf30c0a5, "dma_resv_iter_next_unlocked" },
	{ 0x582f248e, "drm_dp_get_adjust_request_pre_emphasis" },
	{ 0x7d3c2f4, "devm_kmalloc" },
	{ 0xdfc2eb64, "backlight_device_get_by_name" },
	{ 0x5def2f73, "device_link_del" },
	{ 0xe39e3464, "pcpu_hot" },
	{ 0x8a5cf715, "perf_pmu_unregister" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0x9d83e1f4, "cleanup_srcu_struct" },
	{ 0xfb578fc5, "memset" },
	{ 0xece784c2, "rb_first" },
	{ 0xc57c48a3, "idr_get_next" },
	{ 0xad0599c9, "perf_trace_run_bpf_submit" },
	{ 0x4246827a, "pin_user_pages_fast" },
	{ 0xf5f370e0, "async_schedule_node" },
	{ 0x1c4bba9, "drm_dp_lttpr_max_link_rate" },
	{ 0xa5fb9919, "alloc_pages" },
	{ 0x5ce2d71e, "perf_event_sysfs_show" },
	{ 0xc7910e38, "drm_vma_offset_lookup_locked" },
	{ 0xfd5f99c6, "cpufreq_cpu_put" },
	{ 0x2089d2ff, "_dev_warn" },
	{ 0x5d993a21, "drm_dp_pcon_frl_enable" },
	{ 0x35eec315, "mmu_interval_notifier_insert" },
	{ 0xd9adfd72, "single_open" },
	{ 0x7e3277f8, "___drm_dbg" },
	{ 0xe5df5df0, "__pm_runtime_suspend" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0x8184305d, "vga_switcheroo_register_client" },
	{ 0x596d4596, "drm_dp_pcon_frl_prepare" },
	{ 0xa9da693b, "device_remove_bin_file" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x98378a1d, "cc_mkdec" },
	{ 0xe3e57659, "drm_crtc_vblank_restore" },
	{ 0x1ebf6c2a, "pci_power_names" },
	{ 0x8397443d, "gpiod_set_value_cansleep" },
	{ 0x82226c53, "pinctrl_unregister_mappings" },
	{ 0x1d40b6f3, "idr_for_each" },
	{ 0x929d4f7f, "fwnode_handle_get" },
	{ 0x72b2cfa7, "__drm_atomic_helper_connector_reset" },
	{ 0xca3f409d, "param_ops_int" },
	{ 0xa843606e, "drm_dp_dsc_sink_supported_input_bpcs" },
	{ 0xab88b8cd, "request_firmware" },
	{ 0xc5c99a79, "drm_dp_get_adjust_request_voltage" },
	{ 0x58741002, "drm_plane_create_blend_mode_property" },
	{ 0x48879f20, "hrtimer_init" },
	{ 0x598289b2, "ttm_bo_init_reserved" },
	{ 0xcbd3ceb4, "drm_gem_prime_handle_to_fd" },
	{ 0xaed554b2, "drm_connector_attach_vrr_capable_property" },
	{ 0x365ebc6e, "drm_atomic_commit" },
	{ 0x6b53e216, "drm_dp_downstream_max_tmds_clock" },
	{ 0xd5fd90f1, "prepare_to_wait" },
	{ 0xd506bd77, "register_shrinker" },
	{ 0xf9dc180f, "drm_lspcon_set_mode" },
	{ 0x21ef374c, "try_wait_for_completion" },
	{ 0x3f4547a7, "put_unused_fd" },
	{ 0xce8e37e0, "pci_read_config_dword" },
	{ 0xc7a1840e, "llist_add_batch" },
	{ 0xb0e602eb, "memmove" },
	{ 0x22bdb008, "vga_switcheroo_client_fb_set" },
	{ 0xc66bbe25, "pci_map_rom" },
	{ 0x7e0b1b84, "relay_close" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0xa6f1b6d9, "i2c_acpi_find_adapter_by_handle" },
	{ 0x63a7c28c, "bitmap_find_free_region" },
	{ 0x44513b6a, "drm_mode_crtc_set_gamma_size" },
	{ 0x5e332b52, "__var_waitqueue" },
	{ 0xbed0077d, "ttm_bo_vm_access" },
	{ 0x8fa25c24, "xa_find" },
	{ 0xe81c7322, "drm_atomic_helper_connector_duplicate_state" },
	{ 0xc29b063c, "drm_dp_mst_root_conn_atomic_check" },
	{ 0x71550e36, "pci_bus_type" },
	{ 0x9582b9e4, "trace_event_reg" },
	{ 0xe9a5e67f, "intel_graphics_stolen_res" },
	{ 0x124bad4d, "kstrtobool" },
	{ 0xc907446e, "drm_gem_free_mmap_offset" },
	{ 0x70c007a2, "dev_driver_string" },
	{ 0x5f7985a5, "drm_mm_scan_remove_block" },
	{ 0xd4835ef8, "dmi_check_system" },
	{ 0x49e96999, "cond_synchronize_rcu" },
	{ 0xdc6699cb, "acpi_dev_free_resource_list" },
	{ 0x68ee36da, "dma_buf_put" },
	{ 0x2c06610, "ww_mutex_trylock" },
	{ 0x1fd397f8, "drm_modeset_lock_single_interruptible" },
	{ 0xc617f82c, "unregister_oom_notifier" },
	{ 0x2042fea8, "drm_mode_create_dp_colorspace_property" },
	{ 0x8e953dd7, "__tracepoint_mmap_lock_acquire_returned" },
	{ 0xb6341284, "drm_atomic_helper_prepare_planes" },
	{ 0x48c093fb, "_atomic_dec_and_lock_irqsave" },
	{ 0xe87b3461, "sysfs_merge_group" },
	{ 0xa69ae75e, "drm_open" },
	{ 0x6ec25396, "pci_read_config_byte" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0xe914e41e, "strcpy" },
	{ 0xf824c7db, "__drm_printfn_debug" },
	{ 0x53523c39, "acpi_find_child_device" },
	{ 0x49cd25ed, "alloc_workqueue" },
	{ 0x9ab7cc22, "dma_resv_wait_timeout" },
	{ 0xbed2fb32, "shmem_read_mapping_page_gfp" },
	{ 0xf3120d1d, "drm_dp_mst_get_port_malloc" },
	{ 0x5a290250, "hdmi_drm_infoframe_pack_only" },
	{ 0xdef72b73, "drm_gem_prime_fd_to_handle" },
	{ 0x53b954a2, "up_read" },
	{ 0x9f984513, "strrchr" },
	{ 0xcc670bdd, "drm_scdc_set_scrambling" },
	{ 0x524879e0, "mas_find" },
	{ 0x6797d568, "intel_gmch_gtt_get" },
	{ 0x1eda78e3, "ttm_tt_init" },
	{ 0x622c7922, "register_oom_notifier" },
	{ 0xcf00781e, "drm_atomic_helper_disable_plane" },
	{ 0xa6340a8d, "drm_crtc_vblank_off" },
	{ 0x5285fa3b, "__trace_trigger_soft_disabled" },
	{ 0x333bfca1, "hdmi_infoframe_pack_only" },
	{ 0x28899f3a, "drm_atomic_add_affected_connectors" },
	{ 0x19c040c1, "mipi_dsi_dcs_nop" },
	{ 0xdc0e4855, "timer_delete" },
	{ 0xce807a25, "up_write" },
	{ 0x936c834c, "drm_dp_add_payload_part2" },
	{ 0x9c696c42, "drm_dp_add_payload_part1" },
	{ 0x8823ef75, "intel_gmch_gtt_insert_page" },
	{ 0xf69f589d, "drm_edid_read_ddc" },
	{ 0xad299709, "pci_unmap_rom" },
	{ 0x73011db0, "drm_dp_bw_code_to_link_rate" },
	{ 0x64326578, "dma_buf_attach" },
	{ 0x82ee90dc, "timer_delete_sync" },
	{ 0xa916b694, "strnlen" },
	{ 0x8541c497, "ttm_resource_fini" },
	{ 0x5141a6f1, "drm_dp_dpcd_read_link_status" },
	{ 0xf4661c2e, "drm_dp_mst_hpd_irq_handle_event" },
	{ 0xcc43af4f, "cpufreq_cpu_get" },
	{ 0xd524692a, "ttm_bo_eviction_valuable" },
	{ 0x450e5d21, "drm_dev_put" },
	{ 0x3e14635c, "pinctrl_select_state" },
	{ 0x1605d0ed, "drm_dp_lttpr_max_lane_count" },
	{ 0x50fad434, "round_jiffies_up" },
	{ 0x16e297c3, "bit_wait" },
	{ 0xe48024db, "relay_switch_subbuf" },
	{ 0xc48835e7, "sysfs_create_file_ns" },
	{ 0x56aada3c, "__drm_atomic_helper_plane_state_reset" },
	{ 0x5a0b73d0, "zlib_deflateInit2" },
	{ 0x84b16282, "__SCK__tp_func_dma_fence_signaled" },
	{ 0x4b5e3a47, "__get_user_nocheck_1" },
	{ 0xc25e83b4, "hdmi_infoframe_log" },
	{ 0x2d55ad95, "drm_helper_mode_fill_fb_struct" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0xa566d466, "drm_dp_pcon_hdmi_link_active" },
	{ 0xe8580896, "vga_switcheroo_unregister_client" },
	{ 0xfd60df2, "drm_get_connector_status_name" },
	{ 0x4f0e8967, "drm_mode_object_get" },
	{ 0xedde253b, "__drm_dev_dbg" },
	{ 0x7a8f6dc0, "init_srcu_struct" },
	{ 0x15df60c7, "drm_fb_helper_pan_display" },
	{ 0xafe69e32, "drm_dp_downstream_mode" },
	{ 0xff26d24, "kmalloc_trace" },
	{ 0x4b7ebf95, "drm_mm_remove_node" },
	{ 0x4af6ddf0, "kstrtou16" },
	{ 0x8b8ba984, "drm_connector_list_iter_end" },
	{ 0x9ed12e20, "kmalloc_large" },
	{ 0xd9491c14, "xa_destroy" },
	{ 0xa4cfaa1d, "drm_atomic_helper_wait_for_flip_done" },
	{ 0xd28a84fd, "__drm_atomic_helper_plane_destroy_state" },
	{ 0xfe6157b4, "drm_invalid_op" },
	{ 0xb7925412, "vfs_kern_mount" },
	{ 0xbbb046d5, "drm_helper_hpd_irq_event" },
	{ 0xd36dc10c, "get_random_u32" },
	{ 0x22d53779, "drm_buddy_free_list" },
	{ 0x22ec5205, "cpu_latency_qos_remove_request" },
	{ 0x7c73161f, "drm_mode_object_put" },
	{ 0x9566b8bf, "drm_privacy_screen_get" },
	{ 0x335a1ba7, "drm_hdcp_check_ksvs_revoked" },
	{ 0x28779e52, "drm_printf" },
	{ 0x7665a95b, "idr_remove" },
	{ 0xe1c646a8, "drm_crtc_arm_vblank_event" },
	{ 0x87e2a223, "drm_modeset_backoff" },
	{ 0x30e79a27, "__tracepoint_mmap_lock_start_locking" },
	{ 0x5e9d25ff, "pci_sriov_get_totalvfs" },
	{ 0x8f2703b7, "wbinvd_on_all_cpus" },
	{ 0xb52f4b84, "drm_gem_handle_create" },
	{ 0x9f44c898, "drm_buddy_init" },
	{ 0x6eeb5db4, "drm_poll" },
	{ 0xc56f547d, "pci_bus_read_config_byte" },
	{ 0x3b8324ed, "drm_atomic_helper_commit_duplicated_state" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xa47826e4, "drm_dp_calc_pbn_mode" },
	{ 0x34f43af4, "ttm_pool_alloc" },
	{ 0x6383b27c, "__x86_indirect_thunk_rdx" },
	{ 0x4ae90d8e, "hdmi_avi_infoframe_check" },
	{ 0x6b29948f, "dma_alloc_attrs" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x222e7ce2, "sysfs_streq" },
	{ 0xfeb953b1, "__drm_printfn_seq_file" },
	{ 0xd642f3f6, "video_firmware_drivers_only" },
	{ 0x1c033ae9, "dma_resv_fini" },
	{ 0xb246a417, "drm_fb_helper_prepare" },
	{ 0x5b641283, "arch_phys_wc_add" },
	{ 0x789a7ab9, "irq_work_sync" },
	{ 0xb9cad492, "__drm_atomic_state_free" },
	{ 0x3c94ec58, "drm_property_replace_blob" },
	{ 0xa295e784, "drm_edp_backlight_enable" },
	{ 0x7155bcf8, "sync_file_get_fence" },
	{ 0x82bd63ea, "drm_atomic_get_new_mst_topology_state" },
	{ 0x9af51180, "ttm_bo_validate" },
	{ 0xaa87cf19, "__cpuhp_state_add_instance" },
	{ 0x58d8fcaa, "drm_dsc_pps_payload_pack" },
	{ 0x38f73928, "drm_dp_read_desc" },
	{ 0x9fa7184a, "cancel_delayed_work_sync" },
	{ 0x70ad75fb, "radix_tree_lookup" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0xa4191c0b, "memset_io" },
	{ 0x5e0d9705, "kmalloc_caches" },
	{ 0x5bf351bc, "drm_dev_get" },
	{ 0xd59bfed5, "ttm_tt_fini" },
	{ 0xe1718c10, "drm_fb_helper_check_var" },
	{ 0x890fce02, "ttm_resource_free" },
	{ 0x50d1f870, "pgprot_writecombine" },
	{ 0xbf820601, "relay_flush" },
	{ 0xc5e74216, "release_resource" },
	{ 0xd8b06ab0, "seq_putc" },
	{ 0xb7b4209a, "drm_ioctl_kernel" },
	{ 0x5d20f208, "seq_printf" },
	{ 0x20a789ac, "irq_set_chip_data" },
	{ 0x2e7a17d4, "vmap_pfn" },
	{ 0x19ac0ff7, "__cpuhp_remove_state" },
	{ 0xbd5c1d5a, "i2c_add_adapter" },
	{ 0x69dd3b5b, "crc32_le" },
	{ 0xd8312e0e, "drm_syncobj_add_point" },
	{ 0xa084749a, "__bitmap_or" },
	{ 0x69e1bf40, "drm_clflush_sg" },
	{ 0xfbdd080f, "drm_dp_pcon_convert_rgb_to_ycbcr" },
	{ 0x42ae9763, "drm_helper_move_panel_connectors_to_head" },
	{ 0xd4c14632, "system_unbound_wq" },
	{ 0x63f5c260, "unlock_page" },
	{ 0x20977dba, "pci_get_class" },
	{ 0xfba7a5f5, "__get_random_u32_below" },
	{ 0xdc44dbd2, "debugfs_create_file_unsafe" },
	{ 0xccf54d5e, "drm_dp_get_adjust_tx_ffe_preset" },
	{ 0x8e2c7453, "ww_mutex_lock_interruptible" },
	{ 0x37a0cba, "kfree" },
	{ 0xc41dd338, "trace_event_raw_init" },
	{ 0x49cd360e, "put_device" },
	{ 0x7483dc59, "pci_dev_present" },
	{ 0xeb34b6ec, "drm_dp_mst_atomic_wait_for_dependencies" },
	{ 0xc3055d20, "usleep_range_state" },
	{ 0x70fb1e95, "ttm_resource_manager_init" },
	{ 0x9f927032, "drm_noop" },
	{ 0xa849646b, "drm_dp_mst_connector_early_unregister" },
	{ 0xdf930d78, "__pm_runtime_resume" },
	{ 0xc631580a, "console_unlock" },
	{ 0xe7e34bea, "drm_vblank_work_schedule" },
	{ 0x5c3c7387, "kstrtoull" },
	{ 0x349cba85, "strchr" },
	{ 0xee91879b, "rb_first_postorder" },
	{ 0xef592d5, "device_get_next_child_node" },
	{ 0xdeb99119, "dma_fence_init" },
	{ 0x823c19ea, "iosf_mbi_unregister_pmic_bus_access_notifier_unlocked" },
	{ 0x86f6b99d, "synchronize_rcu_expedited" },
	{ 0x175b96de, "drm_fb_helper_debug_leave" },
	{ 0x9c849adf, "drm_atomic_get_plane_state" },
	{ 0x4e68e9be, "rb_next_postorder" },
	{ 0xcbaa04b0, "drm_atomic_helper_page_flip" },
	{ 0x1d07e365, "memdup_user_nul" },
	{ 0x2cd161c0, "drm_dp_dual_mode_max_tmds_clock" },
	{ 0x509af9f0, "gpiod_set_value" },
	{ 0x1551fcf1, "pci_d3cold_enable" },
	{ 0x2688ec10, "bitmap_zalloc" },
	{ 0xfe0645ed, "drm_connector_set_path_property" },
	{ 0x4aa7a3b7, "dma_resv_iter_first_unlocked" },
	{ 0xfbaaf01e, "console_lock" },
	{ 0xb41289e6, "sysfs_remove_bin_file" },
	{ 0x27525c36, "kobject_init" },
	{ 0x6df31390, "intel_gmch_gtt_clear_range" },
	{ 0xb053adda, "drm_rect_rotate" },
	{ 0x1e048714, "init_uts_ns" },
	{ 0x4575a0ca, "drm_mode_set_crtcinfo" },
	{ 0x7c2d03a6, "dma_fence_enable_sw_signaling" },
	{ 0x377bbcbc, "pm_suspend_target_state" },
	{ 0xb1c54cca, "drm_dp_aux_register" },
	{ 0x767b4afa, "relay_buf_full" },
	{ 0x29ad8e33, "x86_hyper_type" },
	{ 0x9c8a19e7, "drm_framebuffer_init" },
	{ 0x82350df, "ww_mutex_lock" },
	{ 0xd38cd261, "__default_kernel_pte_mask" },
	{ 0x6fa48ae9, "drm_color_lut_check" },
	{ 0xa248afde, "drm_detect_monitor_audio" },
	{ 0x46cf10eb, "cachemode2protval" },
	{ 0xffae8e8b, "nsecs_to_jiffies" },
	{ 0x5da0ac15, "pci_rebar_get_possible_sizes" },
	{ 0x10545235, "drm_mode_probed_add" },
	{ 0xf133a2a0, "trace_event_buffer_commit" },
	{ 0x99eaa283, "devm_gpiod_get_index" },
	{ 0x825dfd28, "i2c_get_adapter" },
	{ 0xdb1edee6, "hdmi_infoframe_unpack" },
	{ 0x29eba37f, "current_is_async" },
	{ 0x87706d4e, "__put_user_nocheck_8" },
	{ 0x3b423410, "__copy_user_nocache" },
	{ 0x7f24de73, "jiffies_to_usecs" },
	{ 0xf9ca2eb4, "kstrtoint_from_user" },
	{ 0x7fa4f5c9, "cec_fill_conn_info_from_drm" },
	{ 0x56470118, "__warn_printk" },
	{ 0xcd9c3d7b, "devm_pinctrl_get" },
	{ 0x40d04664, "console_trylock" },
	{ 0xf68848a9, "drm_connector_set_link_status_property" },
	{ 0x77358855, "iomem_resource" },
	{ 0x41c56bee, "dma_buf_unmap_attachment" },
	{ 0x26be8a5f, "mipi_dsi_picture_parameter_set" },
	{ 0x1984d421, "out_of_line_wait_on_bit" },
	{ 0x88faea0c, "drm_atomic_helper_cleanup_planes" },
	{ 0x3e3bad0a, "__tasklet_hi_schedule" },
	{ 0xea6a5dd1, "drm_dp_pcon_pps_override_param" },
	{ 0x45b61916, "acpi_video_register_backlight" },
	{ 0xff748b76, "drm_buddy_alloc_blocks" },
	{ 0xd28d3f02, "drm_connector_attach_scaling_mode_property" },
	{ 0x2d4c773a, "hdmi_spd_infoframe_init" },
	{ 0x6d334118, "__get_user_8" },
	{ 0xfef216eb, "_raw_spin_trylock" },
	{ 0x1a63af34, "vga_switcheroo_process_delayed_switch" },
	{ 0x38722f80, "kernel_fpu_end" },
	{ 0x279f8a7f, "drm_calc_timestamping_constants" },
	{ 0xa72f765, "drm_clflush_virt_range" },
	{ 0x6dc35b25, "radix_tree_iter_delete" },
	{ 0x6fb49676, "queue_rcu_work" },
	{ 0x8c8569cb, "kstrtoint" },
	{ 0xa495ec2c, "drm_connector_cleanup" },
	{ 0xb11ac7a7, "__drm_err" },
	{ 0x838691e1, "ttm_bo_move_accel_cleanup" },
	{ 0x29064135, "drm_helper_probe_single_connector_modes" },
	{ 0x1b0a1fdc, "drm_dp_lttpr_voltage_swing_level_3_supported" },
	{ 0xf93ed358, "sysfs_create_bin_file" },
	{ 0xe5825104, "drm_connector_attach_max_bpc_property" },
	{ 0x1ef98f6d, "sysfs_create_files" },
	{ 0x8f9c199c, "__get_user_2" },
	{ 0xffcd7f49, "iosf_mbi_punit_acquire" },
	{ 0x305609eb, "default_llseek" },
	{ 0x44aaf30f, "tsc_khz" },
	{ 0xe523ad75, "synchronize_irq" },
	{ 0x16d583dd, "drm_property_add_enum" },
	{ 0x2f380b70, "component_del" },
	{ 0xc8dcc62a, "krealloc" },
	{ 0x168df9fa, "drm_dp_read_clock_recovery_delay" },
	{ 0x59038cd1, "i2c_del_adapter" },
	{ 0xfcd1819a, "hdmi_spd_infoframe_check" },
	{ 0x27010df1, "drm_connector_attach_content_type_property" },
	{ 0x6729d3df, "__get_user_4" },
	{ 0xbf86a3d, "drm_connector_unregister" },
	{ 0x655e1044, "dma_set_mask" },
	{ 0x94f89f74, "drmm_kmalloc" },
	{ 0x615911d7, "__bitmap_set" },
	{ 0xde88c520, "drm_crtc_set_max_vblank_count" },
	{ 0xea941420, "firmware_request_nowarn" },
	{ 0x5a6cbc9f, "drm_atomic_helper_shutdown" },
	{ 0x301304c2, "__get_user_nocheck_8" },
	{ 0x8ce1df3, "drm_crtc_vblank_helper_get_vblank_timestamp_internal" },
	{ 0x742e1fda, "pid_task" },
	{ 0x7f757da5, "mipi_dsi_dcs_write" },
	{ 0xa4274666, "pci_bus_alloc_resource" },
	{ 0x584306cf, "drm_plane_create_alpha_property" },
	{ 0xc4bfd2d9, "ttm_bo_put" },
	{ 0x4e4aeb7b, "drm_dp_read_sink_count" },
	{ 0xa3551db1, "drm_dp_dpcd_read_phy_link_status" },
	{ 0x668b19a1, "down_read" },
	{ 0xb6ec7c99, "cfb_copyarea" },
	{ 0x694fdaf9, "device_link_remove" },
	{ 0xe5360b84, "drm_dp_pcon_dsc_max_slices" },
	{ 0x7e2baefd, "drm_dp_aux_init" },
	{ 0x98dcb28e, "backlight_device_unregister" },
	{ 0xe2a8072d, "fput" },
	{ 0xcefbd757, "drm_debugfs_create_files" },
	{ 0xea3c74e, "tasklet_kill" },
	{ 0xb661947e, "drm_hdmi_avi_infoframe_quant_range" },
	{ 0xb22a1f66, "drm_dp_pcon_frl_configure_1" },
	{ 0xe28af695, "relay_open" },
	{ 0x5407ae9e, "drm_dp_get_dual_mode_type_name" },
	{ 0xcee52d8b, "drm_vblank_work_flush" },
	{ 0x3441cf03, "drm_connector_init" },
	{ 0x2a962499, "drm_mm_scan_init_with_range" },
	{ 0x3c96fa2f, "hrtimer_active" },
	{ 0xbdebc432, "drm_plane_create_scaling_filter_property" },
	{ 0xf69b12f5, "__devm_drm_dev_alloc" },
	{ 0x9fa57926, "drm_dp_pcon_frl_configure_2" },
	{ 0x8b419ca4, "kobject_create_and_add" },
	{ 0x8bfd9329, "i2c_bit_algo" },
	{ 0xa6c8cad3, "drm_ioctl" },
	{ 0xc38c83b8, "mod_timer" },
	{ 0xd3a7e73e, "mipi_dsi_set_maximum_return_packet_size" },
	{ 0x829b6048, "drm_dp_dsc_sink_max_slice_count" },
	{ 0x6ac910d0, "drm_any_plane_has_format" },
	{ 0xc19fbf8c, "param_ops_charp" },
	{ 0x32835ee8, "drm_vblank_init" },
	{ 0xbb9ed3bf, "mutex_trylock" },
	{ 0xe07ef363, "intel_gmch_gtt_insert_sg_entries" },
	{ 0xaea50e04, "bus_unregister_notifier" },
	{ 0x525dfefd, "trace_event_buffer_reserve" },
	{ 0x364c23ad, "mutex_is_locked" },
	{ 0x5cbd5228, "drm_atomic_state_default_release" },
	{ 0xef6c3f70, "round_jiffies_up_relative" },
	{ 0xb04a43ad, "__xa_alloc_cyclic" },
	{ 0x248ffa4, "seq_lseek" },
	{ 0x8caf9305, "uuid_is_valid" },
	{ 0x8bebe049, "__mmap_lock_do_trace_start_locking" },
	{ 0x1cfba015, "stackleak_track_stack" },
	{ 0x2d39b0a7, "kstrdup" },
	{ 0x8bde8df2, "irq_work_queue" },
	{ 0x476ef5d4, "drm_dp_set_phy_test_pattern" },
	{ 0xcd88870e, "drm_connector_set_panel_orientation_with_quirk" },
	{ 0x107742a9, "drm_get_subpixel_order_name" },
	{ 0x7f8839d6, "i2c_acpi_get_i2c_resource" },
	{ 0xaf267620, "drm_dp_lttpr_count" },
	{ 0x9f46ced8, "__sw_hweight64" },
	{ 0x2c541e7b, "radix_tree_next_chunk" },
	{ 0xc571d696, "drm_dp_mst_topology_mgr_suspend" },
	{ 0x54efdccf, "drm_crtc_create_scaling_filter_property" },
	{ 0x51f9464e, "wake_up_process" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0xc020c0c1, "drm_dp_pcon_dsc_max_slice_width" },
	{ 0x20978fb9, "idr_find" },
	{ 0x7696f8c7, "__list_add_valid_or_report" },
	{ 0x99fd20aa, "drm_rect_clip_scaled" },
	{ 0xd6b67947, "drm_vblank_work_init" },
	{ 0x4a453f53, "iowrite32" },
	{ 0xd4882770, "sysfs_create_groups" },
	{ 0x9d936575, "drm_dp_vsc_sdp_log" },
	{ 0x2787dc8a, "mmu_interval_read_begin" },
	{ 0x35c7645a, "vga_client_register" },
	{ 0xc30d71cc, "drm_buddy_block_print" },
	{ 0x1ebc7648, "drm_atomic_get_connector_state" },
	{ 0xaa309cf, "synchronize_hardirq" },
	{ 0xcdb99cc9, "drm_mode_init" },
	{ 0x7a81541b, "async_synchronize_cookie" },
	{ 0xb3bc6597, "drm_dp_read_mst_cap" },
	{ 0xf689ad25, "drm_dp_downstream_420_passthrough" },
	{ 0x9e8684a8, "shmem_truncate_range" },
	{ 0x7ff0e6c4, "mark_page_accessed" },
	{ 0x64670fe6, "drm_dp_mst_hpd_irq_send_new_request" },
	{ 0x98555a05, "dma_fence_chain_walk" },
	{ 0xaf07d0d3, "drm_kms_helper_poll_init" },
	{ 0x46df3538, "drm_dp_pcon_hdmi_frl_link_error_count" },
	{ 0xb63045e3, "drm_dp_cec_unregister_connector" },
	{ 0x23daa989, "mipi_dsi_create_packet" },
	{ 0x67f5ff57, "vga_put" },
	{ 0x9114b616, "__xa_alloc" },
	{ 0xf07464e4, "hrtimer_forward" },
	{ 0x205620ff, "pci_unregister_driver" },
	{ 0x29f078d1, "drm_mode_legacy_fb_format" },
	{ 0x4c236f6f, "__x86_indirect_thunk_r15" },
	{ 0xc07351b3, "__SCT__cond_resched" },
	{ 0x503fabbc, "drm_atomic_helper_check_modeset" },
	{ 0x55385e2e, "__x86_indirect_thunk_r14" },
	{ 0x44e85cb0, "drm_fb_helper_ioctl" },
	{ 0x7d393605, "ttm_bo_move_sync_cleanup" },
	{ 0x8a833583, "dma_fence_array_ops" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x90b9e330, "param_ops_uint" },
	{ 0x26b56e1d, "pci_bus_read_config_word" },
	{ 0xc31db0ce, "is_vmalloc_addr" },
	{ 0x7b2e2166, "drm_vma_node_revoke" },
	{ 0x64584c81, "__put_devmap_managed_page_refs" },
	{ 0xd336bfe8, "drm_modeset_acquire_fini" },
	{ 0x4ae2ee57, "drm_connector_attach_hdr_output_metadata_property" },
	{ 0xff81487d, "gpiod_remove_lookup_table" },
	{ 0xd5cedb, "vmap" },
	{ 0x973fa82e, "register_acpi_notifier" },
	{ 0x92b9835e, "drm_dp_128b132b_cds_interlane_align_done" },
	{ 0x350f6ce5, "tasklet_unlock_wait" },
	{ 0xef454088, "drm_dp_atomic_find_time_slots" },
	{ 0x5bd85d51, "backlight_device_register" },
	{ 0xf390f6f1, "__bitmap_andnot" },
	{ 0x3d1363cb, "_dev_err" },
	{ 0xa509bf9c, "find_vma" },
	{ 0x5322a697, "irq_set_chip_and_handler_name" },
	{ 0x5d49aabc, "init_wait_var_entry" },
	{ 0x67cc186a, "drm_atomic_set_crtc_for_plane" },
	{ 0x215fd055, "drm_crtc_send_vblank_event" },
	{ 0x82e6dc97, "unmap_mapping_range" },
	{ 0xfa0c5983, "drm_fb_helper_fill_info" },
	{ 0xee561464, "kobject_init_and_add" },
	{ 0x23961837, "drm_dp_downstream_max_bpc" },
	{ 0x57698a50, "drm_mm_takedown" },
	{ 0xf8d07858, "bitmap_from_arr32" },
	{ 0x52e42ae9, "__drm_atomic_helper_crtc_state_reset" },
	{ 0x5a14d6c8, "ttm_resource_manager_debug" },
	{ 0x933e8495, "drm_connector_atomic_hdr_metadata_equal" },
	{ 0xe9c767de, "drm_dp_mst_dump_topology" },
	{ 0xb383e927, "ttm_tt_populate" },
	{ 0xe0d2a8e9, "drm_gem_dmabuf_release" },
	{ 0x3ca1f9f, "drm_lspcon_get_mode" },
	{ 0x99f2d00a, "sysfs_emit_at" },
	{ 0x89940875, "mutex_lock_interruptible" },
	{ 0x8c03d20c, "destroy_workqueue" },
	{ 0x361c2443, "seq_read" },
	{ 0x9d0d3f7a, "pci_save_state" },
	{ 0x603074f0, "drm_dev_enter" },
	{ 0xb3750192, "drm_edid_valid" },
	{ 0xde80cd09, "ioremap" },
	{ 0xa85a3e6d, "xa_load" },
	{ 0x7ad1ded1, "pinctrl_register_mappings" },
	{ 0x2b24afa, "drm_privacy_screen_put" },
	{ 0x1e3aabd8, "noop_llseek" },
	{ 0xaebd12f0, "acpi_get_name" },
	{ 0x4afb2238, "add_wait_queue" },
	{ 0xfb12fbeb, "drm_property_create" },
	{ 0xda37acd, "dma_fence_array_first" },
	{ 0xdf0ca3f4, "cpu_latency_qos_request_active" },
	{ 0x6b2b69f7, "static_key_enable" },
	{ 0x34266865, "drm_dp_send_power_updown_phy" },
	{ 0x61ca6f1f, "drm_dev_unplug" },
	{ 0xfe052363, "ioread64_lo_hi" },
	{ 0x842c8e9d, "ioread16" },
	{ 0xe123f3d9, "dma_fence_release" },
	{ 0xb8e7ce2c, "__put_user_8" },
	{ 0x5b5ee735, "kernel_param_unlock" },
	{ 0xf797e70c, "__pci_register_driver" },
	{ 0xb8d266a5, "drm_clflush_pages" },
	{ 0x86006267, "drm_gem_dmabuf_export" },
	{ 0x6563b52e, "dma_free_attrs" },
	{ 0x3309a11d, "unregister_shrinker" },
	{ 0xa1fefe6a, "drm_dp_psr_setup_time" },
	{ 0x574c2e74, "bitmap_release_region" },
	{ 0x99f1bbc1, "drm_crtc_add_crc_entry" },
	{ 0xbb8e169a, "vga_switcheroo_handler_flags" },
	{ 0x296e62dc, "_dev_info" },
	{ 0xca9beaa4, "__xa_store" },
	{ 0x1e447bf6, "dma_unmap_sg_attrs" },
	{ 0x8df92f66, "memchr_inv" },
	{ 0xd47dd22d, "drm_mode_config_cleanup" },
	{ 0xe68efe41, "_raw_write_lock" },
	{ 0xfe8c61f0, "_raw_read_lock" },
	{ 0x974420c1, "ttm_kmap_iter_tt_init" },
	{ 0x42548835, "gpiod_put" },
	{ 0x139ae0e8, "fb_io_read" },
	{ 0x8ddd8aad, "schedule_timeout" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xae5a04bb, "acpi_evaluate_dsm" },
	{ 0x9ecdaf1f, "drm_gem_unmap_dma_buf" },
	{ 0x364850b1, "down_write_killable" },
	{ 0xb517b2a4, "intel_gmch_probe" },
	{ 0x5a81287f, "cfb_fillrect" },
	{ 0x120b336a, "__rb_insert_augmented" },
	{ 0xdd97b376, "drm_encoder_cleanup" },
	{ 0xa1734ecd, "drm_connector_attach_colorspace_property" },
	{ 0x48d88a2c, "__SCT__preempt_schedule" },
	{ 0x915f24f4, "kobject_uevent_env" },
	{ 0x494e3393, "vm_get_page_prot" },
	{ 0xe4cb0510, "param_ops_bool" },
	{ 0x868784cb, "__symbol_get" },
	{ 0xf2c43f3f, "zlib_deflate" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x59a9b0f4, "pci_d3cold_disable" },
	{ 0xfb384d37, "kasprintf" },
	{ 0x84d570a2, "drm_helper_probe_detect" },
	{ 0xb5f1884a, "debugfs_attr_read" },
	{ 0x4b750f53, "_raw_spin_unlock_irq" },
	{ 0x60a13e90, "rcu_barrier" },
	{ 0xb2fd5ceb, "__put_user_4" },
	{ 0xc80202cb, "drm_property_create_blob" },
	{ 0x4ade61fa, "drm_dp_remove_payload" },
	{ 0x9ac0c85b, "drm_object_attach_property" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0x6ee44e9d, "drm_gem_prime_mmap" },
	{ 0xeab2a69d, "drm_dp_cec_unset_edid" },
	{ 0x1a79c8e9, "__x86_indirect_thunk_r13" },
	{ 0xa0d3456d, "nr_swap_pages" },
	{ 0x885b54cc, "__srcu_read_lock" },
	{ 0x688e72e1, "__SCT__preempt_schedule_notrace" },
	{ 0x362f9a8, "__x86_indirect_thunk_r12" },
	{ 0x2d3b3c17, "drm_dp_dpcd_write" },
	{ 0x1b51547d, "drm_fb_helper_initial_config" },
	{ 0xec788566, "acpi_target_system_state" },
	{ 0xe091c977, "list_sort" },
	{ 0x284faa6b, "__x86_indirect_thunk_r11" },
	{ 0x4854c5b, "drm_connector_list_iter_begin" },
	{ 0x31549b2a, "__x86_indirect_thunk_r10" },
	{ 0xe8a0e334, "drm_vma_offset_add" },
	{ 0x248efd3, "kstrtobool_from_user" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x8ef2968b, "drm_dp_dpcd_read" },
	{ 0x8ee1c8b7, "drm_atomic_helper_commit_hw_done" },
	{ 0x12228f65, "set_page_dirty" },
	{ 0xbbda18c4, "dma_fence_free" },
	{ 0xe3ac15d5, "drm_mode_is_420_only" },
	{ 0x9c52e514, "handle_simple_irq" },
	{ 0x35137b3c, "drm_atomic_get_mst_payload_state" },
	{ 0x8b5570f1, "pci_set_power_state" },
	{ 0xebff9b54, "drm_plane_enable_fb_damage_clips" },
	{ 0xc8b6a8ae, "drm_dp_128b132b_lane_channel_eq_done" },
	{ 0x651a4139, "test_taint" },
	{ 0x5e3240a0, "__cpu_online_mask" },
	{ 0x74fc6fbd, "drm_format_info_block_width" },
	{ 0x1a411479, "drm_syncobj_free" },
	{ 0x28aa6a67, "call_rcu" },
	{ 0xc1eb4f72, "simple_attr_write" },
	{ 0xa6457c89, "hdmi_infoframe_pack" },
	{ 0x6a847134, "ttm_kmap_iter_iomap_init" },
	{ 0x57c88434, "apply_to_page_range" },
	{ 0x47cfd825, "kstrtouint_from_user" },
	{ 0x66b4cc41, "kmemdup" },
	{ 0x950eb34e, "__list_del_entry_valid_or_report" },
	{ 0xcada59af, "fd_install" },
	{ 0x1a5bf3ca, "drm_dsc_dp_rc_buffer_size" },
	{ 0xe4fc1cc7, "drm_probe_ddc" },
	{ 0x56a9d865, "drm_property_blob_get" },
	{ 0x4cea1ca6, "drm_connector_init_with_ddc" },
	{ 0xf7d833d5, "drm_connector_attach_privacy_screen_provider" },
	{ 0xfe916dc6, "hex_dump_to_buffer" },
	{ 0x76ff6644, "drm_dp_lttpr_pre_emphasis_level_3_supported" },
	{ 0xd68e0a0, "drm_universal_plane_init" },
	{ 0x36897e69, "drm_crtc_enable_color_mgmt" },
	{ 0xdfd1d5b4, "drm_dp_mst_get_edid" },
	{ 0x2053aaf8, "drm_dp_mst_topology_mgr_init" },
	{ 0xa07d1b3c, "tasklet_setup" },
	{ 0xaa0c318b, "vscnprintf" },
	{ 0x8ce59077, "__drm_atomic_helper_connector_duplicate_state" },
	{ 0xaa50a861, "pagecache_get_page" },
	{ 0x2d50570f, "drm_rect_calc_hscale" },
	{ 0x39c63e50, "drm_atomic_state_default_clear" },
	{ 0x15a90363, "put_pid" },
	{ 0x9f0450d5, "perf_pmu_register" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0x6027d703, "pm_runtime_get_if_active" },
	{ 0x74815c9a, "mipi_dsi_generic_write" },
	{ 0x5bb256fe, "drm_atomic_add_affected_planes" },
	{ 0x77a33ac2, "__pm_runtime_idle" },
	{ 0x7de7bf50, "__acpi_video_get_backlight_type" },
	{ 0x44c10a52, "kvfree_call_rcu" },
	{ 0xfa150882, "drm_buddy_fini" },
	{ 0x53569707, "this_cpu_off" },
	{ 0xd5a95eae, "drm_dp_128b132b_lane_symbol_locked" },
	{ 0x8f74a2c2, "drm_dp_mst_topology_mgr_resume" },
	{ 0xc8c85086, "sg_free_table" },
	{ 0xf070de00, "auxiliary_device_init" },
	{ 0xfdf1f2dd, "drm_atomic_helper_commit_cleanup_done" },
	{ 0xa339e6e5, "on_each_cpu_cond_mask" },
	{ 0x31b64c1e, "kmem_cache_create" },
	{ 0xe31b9301, "intel_gmch_gtt_flush" },
	{ 0xa0ebd437, "hdmi_drm_infoframe_check" },
	{ 0x4a1458de, "drm_connector_update_edid_property" },
	{ 0x20169430, "drm_edp_backlight_disable" },
	{ 0x80705dab, "pci_iounmap" },
	{ 0xa9f0a8b7, "drm_fb_helper_alloc_info" },
	{ 0x6c184cb7, "drm_dp_aux_unregister" },
	{ 0x4a666d56, "hrtimer_cancel" },
	{ 0xe9817e0d, "drm_atomic_helper_suspend" },
	{ 0xf805e0bb, "drm_dp_dpcd_probe" },
	{ 0x197d5b60, "dma_fence_default_wait" },
	{ 0xabb5a026, "drm_buddy_block_trim" },
	{ 0x91fec1cc, "drm_rect_calc_vscale" },
	{ 0x7410aba2, "strreplace" },
	{ 0x6ebe366f, "ktime_get_mono_fast_ns" },
	{ 0xa02aa74a, "__cond_resched_lock" },
	{ 0x37169f79, "cpu_latency_qos_update_request" },
	{ 0x5ef365bc, "shmem_file_setup_with_mnt" },
	{ 0xa843805a, "get_unused_fd_flags" },
	{ 0x6e130744, "cfb_imageblit" },
	{ 0x7a55af08, "drm_crtc_cleanup" },
	{ 0xb3f985a8, "sg_alloc_table" },
	{ 0xdf36914b, "xa_find_after" },
	{ 0x1c5b1f28, "irq_free_descs" },
	{ 0x44b3c6ed, "dma_fence_array_create" },
	{ 0xc890c008, "zlib_deflateEnd" },
	{ 0x1e2c3a09, "drm_atomic_helper_duplicate_state" },
	{ 0xca21ebd3, "bitmap_free" },
	{ 0x7e99bab3, "drm_atomic_get_old_mst_topology_state" },
	{ 0x65702bd6, "drm_default_rgb_quant_range" },
	{ 0x5229c003, "drm_atomic_state_alloc" },
	{ 0xf45dd266, "drm_dp_get_phy_test_pattern" },
	{ 0x76d496e, "single_open_size" },
	{ 0xcb2340b8, "drm_rect_debug_print" },
	{ 0xc6d09aa9, "release_firmware" },
	{ 0xd680a377, "drm_gem_object_free" },
	{ 0x8e17b3ae, "idr_destroy" },
	{ 0xe8f4f52e, "pci_write_config_byte" },
	{ 0xf1969a8e, "__usecs_to_jiffies" },
	{ 0x4a3ad70e, "wait_for_completion_timeout" },
	{ 0x782018b7, "drm_dp_mst_put_port_malloc" },
	{ 0xce54cbb2, "drm_kms_helper_poll_enable" },
	{ 0x953e1b9e, "ktime_get_real_seconds" },
	{ 0xcd91b127, "system_highpri_wq" },
	{ 0x275f3d49, "hdmi_vendor_infoframe_check" },
	{ 0xa344867f, "drm_connector_attach_dp_subconnector_property" },
	{ 0x9eff5ed0, "devm_pinctrl_put" },
	{ 0x5322663e, "acpi_get_handle" },
	{ 0x6094ae54, "drm_modeset_unlock" },
	{ 0x40235c98, "_raw_write_unlock" },
	{ 0x85afc11d, "device_del" },
	{ 0x1000e51, "schedule" },
	{ 0x551bd071, "__rb_erase_color" },
	{ 0xe3feba56, "tasklet_unlock_spin_wait" },
	{ 0x6fbc6a00, "radix_tree_insert" },
	{ 0x21f93669, "register_sysctl_sz" },
	{ 0x8c7765a3, "drm_dp_dual_mode_set_tmds_output" },
	{ 0x7f95699c, "device_link_add" },
	{ 0xdf3f760d, "drm_mm_scan_color_evict" },
	{ 0x734257b2, "dma_max_mapping_size" },
	{ 0x5aa515f7, "drm_crtc_accurate_vblank_count" },
	{ 0x2fe1004b, "trace_event_printf" },
	{ 0xeeaf02f3, "get_task_pid" },
	{ 0x6f9169ab, "drm_dp_atomic_release_time_slots" },
	{ 0x755b6292, "drm_modeset_acquire_init" },
	{ 0xffc0205d, "sysfs_remove_group" },
	{ 0xadbeed61, "mipi_dsi_packet_format_is_long" },
	{ 0x71fb6892, "dma_fence_array_next" },
	{ 0x6e516833, "mmu_interval_notifier_remove" },
	{ 0x44ef2fb1, "drm_atomic_state_clear" },
	{ 0x57bc19d2, "down_write" },
	{ 0xe8093e53, "seq_puts" },
	{ 0x86dd1365, "debugfs_create_bool" },
	{ 0x2754dad8, "drm_mm_reserve_node" },
	{ 0x42de47c8, "__drmm_add_action_or_reset" },
	{ 0x1057a279, "bsearch" },
	{ 0xa5526619, "rb_insert_color" },
	{ 0xf6da7a98, "drm_atomic_helper_swap_state" },
	{ 0xf0517d7a, "drm_mm_init" },
	{ 0x731dba7a, "xen_domain_type" },
	{ 0xa6989089, "pci_vfs_assigned" },
	{ 0xe7a02573, "ida_alloc_range" },
	{ 0xf601571f, "kmem_cache_alloc" },
	{ 0x3a646ba6, "module_layout" },
};

MODULE_INFO(depends, "video,drm,drm_display_helper,drm_kms_helper,ttm,intel-gtt,hwmon,drm_buddy,cec,i2c-algo-bit");

MODULE_ALIAS("pci:v00008086d00003577sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002562sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003582sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000358Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002572sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002582sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000258Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002592sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002772sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000027A2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000027AEsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002972sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002982sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002992sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000029A2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000029B2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000029C2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000029D2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002A02sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002A12sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002A42sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002E02sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002E12sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002E22sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002E32sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002E42sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00002E92sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A001sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A011sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000042sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000046sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000102sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000010Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000112sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000122sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000106sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000116sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000126sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000016Asv0000152Dsd00008990bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000156sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000166sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000152sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000015Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000162sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000016Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A02sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A06sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A0Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A0Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A0Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000402sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000406sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000040Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000040Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000040Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C02sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C06sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C0Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C0Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C0Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D02sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D06sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D0Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D0Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D0Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A12sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A16sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A1Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A1Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A1Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000412sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000416sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000041Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000041Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000041Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C12sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C16sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C1Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C1Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C1Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D12sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D16sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D1Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D1Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D1Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A22sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A26sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A2Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A2Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A2Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000422sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000426sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000042Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000042Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000042Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C22sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C26sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C2Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C2Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000C2Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D22sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D26sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D2Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D2Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000D2Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000F30sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000F31sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000F32sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000F33sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001606sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000160Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000160Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001602sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000160Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000160Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001616sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000161Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000161Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001612sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000161Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000161Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001626sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000162Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000162Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001622sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000162Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000162Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001636sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000163Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000163Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001632sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000163Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000163Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000022B0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000022B1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000022B2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000022B3sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001906sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001913sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000190Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001915sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001902sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000190Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000190Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001917sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001916sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001921sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000191Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001912sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000191Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000191Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000191Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001923sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001926sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001927sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000192Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000192Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000192Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001932sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000193Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000193Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000193Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00000A84sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001A84sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00001A85sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005A84sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005A85sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003184sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003185sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005906sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005913sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000590Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005915sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005902sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005908sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000590Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000590Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005916sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005921sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000591Esv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005912sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005917sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000591Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000591Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000591Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005926sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005923sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005927sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000593Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000591Csv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000087C0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E90sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E93sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E99sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E91sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E92sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E96sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E98sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E9Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E9Csv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E94sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003E9Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA9sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA5sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA6sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA7sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA8sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA4sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA3sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000087CAsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00003EA2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BA2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BA4sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BA5sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BA8sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BC2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BC4sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BC5sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BC6sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BC8sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BE6sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BF6sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009B21sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BAAsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BACsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009B41sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BCAsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009BCCsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A50sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A52sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A53sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A54sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A56sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A57sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A58sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A59sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A5Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A5Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A5Csv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A70sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A71sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A51sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00008A5Dsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004541sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004551sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004555sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004557sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004570sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004571sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004E51sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004E55sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004E57sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004E61sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004E71sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009A60sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009A68sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009A70sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009A40sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009A49sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009A59sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009A78sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009AC0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009AC9sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009AD9sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00009AF8sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004C80sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004C8Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004C8Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004C8Csv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004C90sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004C9Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004680sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004682sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004688sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000468Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000468Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004690sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004692sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004693sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046A0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046A1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046A2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046A3sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046A6sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046A8sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046AAsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000462Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004626sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004628sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046B0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046B1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046B2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046B3sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046C0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046C1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046C2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046C3sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046D0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046D1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000046D2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004905sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004906sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004907sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004908sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00004909sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A780sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A781sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A782sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A783sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A788sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A789sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A78Asv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A78Bsv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A721sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A7A1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A7A9sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A720sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A7A0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d0000A7A8sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005690sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005691sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005692sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056A0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056A1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056A2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005693sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005694sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005695sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056A5sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056A6sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056B0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056B1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005696sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00005697sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056A3sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056A4sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056B2sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056B3sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056C0sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d000056C1sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00007D40sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00007D60sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00007D45sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00007D55sv*sd*bc03sc*i*");
MODULE_ALIAS("pci:v00008086d00007DD5sv*sd*bc03sc*i*");
