add_executable(InjDemo
	Enum.c
	InjDemo.c
	List.c
)
target_link_libraries(InjDemo InjLib comctl32)