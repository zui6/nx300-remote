# 相机设置字段字典（由 Ghidra 反编译 + 调用点常量索引推导）

| case | 字段地址 | 代表性函数 | 全部使用函数 |
|---|---|---|---|
| 0x02 | 0x004df5c0 | UI_Set_Ae_Lock | UI_Set_Ae_Lock |
| 0x03 | 0x004df5c4 | UI_Set_All_Attributes | UI_Excute_Apex_UpdateAeInfo2, UI_Reset_EVC_Info, UI_Set_All_Attributes |
| 0x04 | 0x004df5c8 | set_ev | DrawTest, set_ev |
| 0x05 | 0x004df5cc | set_av | set_av, set_tv |
| 0x07 | 0x004df5dc | CheckSSChange | CheckSSChange, ReUpdateButton |
| 0x08 | 0x004df5e0 | CheckFnoChange | CheckFnoChange |
| 0x09 | 0x004df5e4 | UI_Set_All_Attributes | UI_Set_All_Attributes, UI_Set_Fno_Info |
| 0x0a | 0x004df5e8 | UI_Set_All_Attributes | UI_Set_All_Attributes, UI_Set_ShutterSpeed_Info |
| 0x0b | 0x004df5ec | UI_Set_Lock3A_Info | UI_Set_Lock3A_Info |
| 0x0c | 0x004df5f0 | set_flash | set_flash |
| 0x0d | 0x004df5f4 | set_histogram | UI_Set_Histogram_Value, set_histogram |
| 0x0e | 0x004df5f8 | set_histogram | UI_Set_Histogram_Value, set_histogram |
| 0x14 | 0x004df608 | set_af_area | UI_Check_Display_Date_Time, UI_Set_Capture_Sub_State_By_Modechange, UI_Set_Histogram_Value, set_af_area, set_battery |
| 0x16 | 0x004df618 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x17 | 0x004df61c | set_wb_custom | UI_Set_All_Attributes, UI_Set_Attributes, UpdateDisk, get_photo_ratio, recreateContents_iso |
| 0x18 | 0x004df640 | UI_Set_All_Attributes | UI_Is_Enable_Capture_Possible, UI_Set_All_Attributes |
| 0x1a | 0x004df648 | set_iso | UI_Set_All_Attributes, UI_Set_Attributes, set_iso |
| 0x1b | 0x004df64c | set_iso | UI_Set_All_Attributes, UI_Set_Attributes, set_iso |
| 0x1c | 0x004df650 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x1e | 0x004df658 | set_white_balance | UI_Handle_Reset_User_Menu_Value, UI_Set_Capture_Menu_Value, set_white_balance |
| 0x24 | 0x004df670 | set_rec | UI_Check_One_Touch_Raw_Enable_Mode, UI_Get_DB_ID_Capture_Menu, UI_Get_Video_Bit_Rate, UI_Set_All_Attributes, set_rec |
| 0x25 | 0x004df674 | UI_Set_All_Attributes | UI_Set_All_Attributes, UI_Set_Capture_Menu_Value |
| 0x26 | 0x004df678 | set_gg_picture_wizard | UI_Handle_Reset_User_Menu_Value, UI_Set_Capture_Menu_Value, set_gg_picture_wizard |
| 0x27 | 0x004df67c | set_gg_picture_wizard | UI_Handle_Reset_User_Menu_Value, UI_Set_Capture_Menu_Value, set_gg_picture_wizard |
| 0x28 | 0x004df680 | set_gg_picture_wizard | UI_Handle_Reset_User_Menu_Value, UI_Set_Capture_Menu_Value, set_gg_picture_wizard |
| 0x29 | 0x004df684 | set_gg_picture_wizard | UI_Handle_Reset_User_Menu_Value, UI_Set_Capture_Menu_Value, set_gg_picture_wizard |
| 0x2a | 0x004df688 | set_af_area | UI_Check_FD_Enable_Value, UI_Is_Enable_Capture_Possible, UI_Reset_AF_Frame_State, UI_Set_All_Attributes, UI_Set_Capture_Sub_State_By_Modechange |
| 0x2b | 0x004df68c | UI_Set_All_Attributes | UI_Check_FD_Enable_Value, UI_Reset_AF_Frame_State, UI_Set_All_Attributes, UI_Set_SmartAuto_State |
| 0x2c | 0x004df694 | set_target | UI_Reset_Selection_Af_Position, set_target |
| 0x2d | 0x004df698 | set_target | UI_Reset_Selection_Af_Position, set_target |
| 0x30 | 0x004df6a4 | set_target | UI_Reset_Selection_Af_Position, set_target |
| 0x31 | 0x004df690 | set_touchAF_state | UI_Check_Tracking_Win_Enable, UI_Set_All_Attributes, UI_Update_FD_Box_Draw_Info, set_touchAF_state |
| 0x32 | 0x004df6b8 | set_mf_foucs_aid | UI_Set_All_Attributes, set_mf_foucs_aid |
| 0x33 | 0x004df6bc | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x34 | 0x004df6c0 | set_metering_spot_target | UI_Set_All_Attributes, set_metering_spot_target |
| 0x35 | 0x004df6c4 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x36 | 0x004df6c8 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x37 | 0x004df6cc | UI_Set_All_Attributes | Check_SelfTimer_Enable, UI_Check_One_Touch_Raw_Enable_Mode, UI_Check_Single_Drive, UI_Get_Shutter_Speed_High_Limit, UI_Get_Shutter_Speed_Low_Limit |
| 0x3a | 0x004df6d8 | set_mode | set_mode, set_panorama_run |
| 0x48 | 0x004df70c | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x4a | 0x004df714 | UI_Check_PW_BRK_Setting | UI_Check_PW_BRK_Setting |
| 0x4b | 0x004df718 | set_flash | UI_Set_All_Attributes, UI_Set_DB_Value_Capture_Menu, Ui_WiFi_Rvf_Check_External_Flash, f_get_camera_conf, set_flash |
| 0x4c | 0x004df71c | set_flash | set_flash |
| 0x4d | 0x004df720 | set_metering_spot_target | UI_Set_All_Attributes, set_metering_spot_target |
| 0x4e | 0x004df724 | UI_Set_All_Attributes | UI_Get_Shutter_Speed_Low_Limit, UI_Set_All_Attributes, recreateContents_iso |
| 0x4f | 0x004df728 | UI_Set_All_Attributes | UI_Set_All_Attributes, UI_Set_Common_Menu_After_File_List |
| 0x50 | 0x004df620 | UI_Set_Attributes | UI_Get_Movie_Remaian_Time, UI_Set_Attributes, get_movie_ratio |
| 0x51 | 0x004df624 | set_av | UI_Is_Enable_Capture_Possible, UI_Set_All_Attributes, UI_Set_Fno_Info, UI_Set_ShutterSpeed_Info, set_av |
| 0x52 | 0x004df628 | UI_Get_Audio_Bit_Rate | UI_Get_Audio_Bit_Rate, UI_Get_Video_Bit_Rate |
| 0x53 | 0x004df62c | UI_Set_All_Attributes | UI_Get_Movie_Remaian_Time, UI_Set_All_Attributes, get_rec_speed |
| 0x54 | 0x004df630 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x55 | 0x004df634 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x56 | 0x004df638 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x57 | 0x004df72c | UI_Set_All_Attributes | UI_Set_All_Attributes, UI_Set_Attributes, UI_Set_Exceptional_Value_And_Dim_On_Item_Change, UpdateDiskIsoStep, recreateContents_iso |
| 0x59 | 0x004df734 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x5a | 0x004df738 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x5b | 0x004df73c | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x5c | 0x004df740 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x5d | 0x004df744 | set_mf_foucs_aid | UI_Set_All_Attributes, set_mf_foucs_aid |
| 0x5e | 0x004df748 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x5f | 0x004df74c | UI_Set_All_Attributes | UI_Is_Enable_Capture_Possible, UI_Set_All_Attributes |
| 0x60 | 0x004df750 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x61 | 0x004df754 | UI_Set_Capture_Menu_Value | UI_Set_Capture_Menu_Value |
| 0x62 | 0x004df758 | UI_Set_Capture_Menu_Value | UI_Set_Capture_Menu_Value |
| 0x63 | 0x004df75c | UI_Set_Capture_Menu_Value | UI_Set_Capture_Menu_Value |
| 0x64 | 0x004df760 | UI_Set_Capture_Menu_Value | UI_Set_Capture_Menu_Value |
| 0x65 | 0x004df764 | UI_Set_Capture_Menu_Value | UI_Set_Capture_Menu_Value |
| 0x66 | 0x004df768 | UI_Set_Capture_Menu_Value | UI_Set_Capture_Menu_Value |
| 0x67 | 0x004df76c | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x6a | 0x004df778 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x6c | 0x004df780 | set_flash | set_flash |
| 0x6d | 0x004df784 | set_date_time | UI_Check_Display_Date_Time, set_date_time |
| 0x6e | 0x004df788 | set_histogram | UI_Set_Histogram_Value, set_histogram |
| 0x70 | 0x004df790 | UI_Handle_Reset_Commom_Menu_Value | UI_Handle_Reset_Commom_Menu_Value, UI_Handle_Reset_User_Menu_Value |
| 0x71 | 0x004df794 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0x72 | 0x004df7a8 | UI_Set_ShutterSpeed_Info | UI_Key_iFn_Set_Shutter_Speed_Info, UI_Set_ShutterSpeed_Info |
| 0x75 | 0x004df7a0 | set_target | UI_Is_Enable_Capture_Possible, UI_Set_Lock3A_Info, set_target |
| 0x76 | 0x004df7a4 | set_target | UI_Reset_AF_Frame_State, UI_Set_Lock3A_Info, UI_Update_FD_Box_Draw_Info, set_target |
| 0x77 | 0x004df7f4 | set_multi_target_box | set_multi_target_box |
| 0x78 | 0x004df7f8 | set_multi_target_box | set_multi_target_box |
| 0x79 | 0x004df7fc | set_multi_target_box | set_multi_target_box |
| 0x7a | 0x004df7ac | UI_Check_Enable_By_Memory | UI_Check_Enable_By_Memory, UI_Draw_Memory_Info, f_get_camera_conf |
| 0x7b | 0x004df7b0 | UI_Check_Enable_By_Memory | UI_Check_Enable_By_Memory, UI_Draw_Memory_Info |
| 0x7c | 0x004df7b4 | set_rec | UI_Check_Enable_By_Memory, UI_Draw_Memory_Info, UI_Get_Movie_Remaian_Time, set_rec, set_rec_update |
| 0x7d | 0x004df7b8 | set_av | set_av, set_mf_distance |
| 0x7e | 0x004df7bc | set_av | UI_Check_WiFi_ModeChange_Enable, UI_Is_Enable_Capture_Possible, UI_Set_SmartAuto_State, set_av, set_lens_ftmf_16_9 |
| 0x7f | 0x004df7c0 | set_av | UI_Check_WiFi_ModeChange_Enable, UI_Set_SmartAuto_State, set_av, set_lens_ftmf_16_9, set_lens_ftmf_3_2 |
| 0x85 | 0x004df7d8 | UI_Check_Active_Event | UI_Check_Active_Event |
| 0x86 | 0x004df7dc | set_mf_distance | set_mf_distance |
| 0x87 | 0x004df7e0 | set_metering_spot_target | UI_Is_Enable_Capture_Possible, UI_Update_FD_Box_Draw_Info, set_metering_spot_target |
| 0x9c | 0x004df850 | set_lens_ftmf_16_9 | UI_Set_Lens_Callback_Fn, set_lens_ftmf_16_9, set_lens_ftmf_3_2, set_mf_distance |
| 0x9d | 0x004df7e4 | set_mf_foucs_aid | set_mf_foucs_aid |
| 0x9e | 0x004df7e8 | set_mf_foucs_aid | set_mf_foucs_aid |
| 0x9f | 0x004df7ec | set_mf_foucs_aid | set_mf_foucs_aid |
| 0xa0 | 0x004df7f0 | set_mf_foucs_aid | set_mf_foucs_aid |
| 0xa3 | 0x004df894 | CheckFnoChange | CheckFnoChange |
| 0xa5 | 0x004df89c | CheckSSChange | CheckSSChange |
| 0xac | 0x004df8b8 | UI_Set_Panorama_Capture_Info | UI_Set_Panorama_Capture_Info |
| 0xad | 0x004df8bc | UI_Set_Panorama_Capture_Info | UI_Set_Panorama_Capture_Info |
| 0xae | 0x004df8c0 | set_panorama_run | UI_Set_Panorama_Capture_Info, set_panorama_run |
| 0xb1 | 0x004df8c4 | set_Tripod_water_Level | set_Tripod_water_Level, set_histogram |
| 0xb2 | 0x004df8c8 | set_mode | UI_Check_WiFi_ModeChange_Enable, UI_Get_File_Name, UI_Set_All_Attributes, UI_Set_Lens_Callback_Fn, set_mode |
| 0xb3 | 0x004df8cc | set_ev | set_ev |
| 0xb6 | 0x004df854 | set_mf_distance | set_mf_distance |
| 0xb7 | 0x004df858 | set_mf_distance | set_mf_distance |
| 0xbb | 0x004df868 | set_shot_to_link | UI_Check_WiFi_ModeChange_Enable, UI_Is_Enable_Capture_Possible, UI_Set_Dim_File_Writing, UI_Set_PM_Lock, UI_Set_PM_Unlock |
| 0xc3 | 0x004df87c | UI_Get_Movie_Remaian_Time | UI_Get_Movie_Remaian_Time, UI_Get_Video_Bit_Rate |
| 0xc4 | 0x004df63c | UI_Set_Attr_Movie_Size | UI_Set_Attr_Movie_Size |
| 0xca | 0x004df8d8 | UI_Set_Capture_Menu_Info_By_GUI | UI_Set_Capture_Menu_Info_By_GUI |
| 0xcb | 0x004df8dc | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0xcc | 0x004df884 | UI_Check_Active_Event | UI_Check_Active_Event |
| 0xcd | 0x004df888 | set_rec_guide_msg | set_rec_guide_msg |
| 0xd1 | 0x004df8ec | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0xd2 | 0x004df8f0 | UI_Set_All_Attributes | UI_Set_All_Attributes |
| 0xd4 | 0x004df8f4 | set_focal_length | UI_RVF_Set_Lens_Zoom_Point, set_focal_length, set_rec_guide_msg |
| 0xd5 | 0x004df900 | set_focal_length | f_get_camera_conf, set_focal_length |
| 0xd7 | 0x004df85c | set_lens_ftmf_16_9 | UI_Reset_Key_Common_Menu_Value, set_lens_ftmf_16_9, set_lens_ftmf_3_2 |
| 0xd8 | 0x004df860 | set_lens_ftmf_16_9 | set_lens_ftmf_16_9, set_lens_ftmf_3_2 |
