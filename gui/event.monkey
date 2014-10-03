Strict



Import gui



Const EVENT_SECRET:Int = -1
Const EVENT_MOUSE_MOVE:Int = 0
Const EVENT_MOUSE_DOWN_LEFT:Int = 1
Const EVENT_MOUSE_DOWN_RIGHT:Int = 2
Const EVENT_MOUSE_DRAG_LEFT:Int = 3
Const EVENT_MOUSE_DRAG_RIGHT:Int = 4
Const EVENT_MOUSE_UP_LEFT:Int = 5
Const EVENT_MOUSE_UP_RIGHT:Int = 6
Const EVENT_MOUSE_DOUBLE_CLICK_LEFT:Int = 7
Const EVENT_MOUSE_DOUBLE_CLICK_RIGHT:Int = 8
'TODO
Const EVENT_MOUSE_ENTER:Int = 5
Const EVENT_MOUSE_LEAVE:Int = 6



Global MOUSE_STATE_NONE:Int = 0
Global MOUSE_STATE_LEFT:Int = 1
Global MOUSE_STATE_RIGHT:Int = 2
Global MOUSE_STATE_BOTH_LEFT:Int = 3
Global MOUSE_STATE_BOTH_RIGHT:Int = 4



Class Event
	Global globalWindow:WindowGadget
	
	Field window:WindowGadget
	Field id:Int
	Field x:Int, y:Int
	Field dx:Int, dy:Int
	Field destination:Gadget
	
	Method New( id:Int, window:WindowGadget = globalWindow )
		Self.id = id
		Self.window = window
		x = window._mouseX; y = window._mouseY
		
		Select id
			Case EVENT_MOUSE_DRAG_LEFT, EVENT_MOUSE_DRAG_RIGHT, EVENT_MOUSE_UP_LEFT, EVENT_MOUSE_UP_RIGHT
				dx = x - window._mouseDragX; dy = y - window._mouseDragY
				destination = window._destination
			Default
		End
	End
End