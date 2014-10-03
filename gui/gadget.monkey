Strict



Import gui



Class Gadget
	Field parent:Gadget
	Field x:Int, y:Int
	Field w:Int, h:Int
	
	Method xTranslate:Int() Property
		Return x
	End
	
	Method yTranslate:Int() Property
		Return y
	End
	
	Method HandleEvent:Gadget( event:Event ) Abstract
	
	Method HandleGadgetEvent:Void( event:GadgetEvent )
		If parent <> Null
			parent.HandleGadgetEvent( event )
		EndIf
	End
	
	Method Render:Void()
		OnRender()
	End
	
	Method OnRender:Void()
		
	End
	
	'TODO
	Method LocalX:Int( _x:Int )
		If parent <> Null Then _x = parent.LocalX( _x )
		Return _x - x''' - ox
	End
	
	Method LocalY:Int( _y:Int )
		If parent <> Null
			_y = parent.LocalY( _y )
		EndIf
		
		Return _y - y''' - oy
	End
	
	Method GlobalX:Int( _x:Int )
		_x = _x + x''' + ox
		If parent <> Null Then _x = parent.GlobalX( _x )
		Return _x
	End
	
	Method GlobalY:Int( _y:Int )
		_y = _y + y''' + oy
		If parent <> Null Then _y = parent.GlobalY( _y )
		Return _y
	End
End