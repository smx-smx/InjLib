add_executable(InjDemo WIN32
	Enum.c
	InjDemo.c
	List.c
	InjLib.rc
)
target_link_libraries(InjDemo InjLib comctl32)
set_target_properties(InjDemo
	PROPERTIES
		WIN32_EXECUTABLE TRUE
)