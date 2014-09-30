Const SCHEME_2:Int = 2



Type Box
	Global idNext:Int
	
	Field id:Int
	Field kind:String
	Field settings:TMap = CreateMap()
	Field x:Int, y:Int, w:Int = 23, h:Int = 21, gap:Int = 15
	Field ins:Int = 2, outs:Int = 1
	
	Field state:Int[ 20, 15 ]
	Field done:Byte = False
	
	Method GetFloatProperty:Int( name:String )
		Return Int( Setting( MapValueForKey( settings, name ) ).value ) * 5 + 5
	EndMethod
	
	Method GetIntProperty:Int( name:String )
		Return Int( Setting( MapValueForKey( settings, name ) ).value )
	EndMethod
	
	Method IsClickable:Byte()
		Return ins = 0 And kind <> "omni" And kind <> "in"
	EndMethod
	
	Function Make:Box( x:Int, y:Int, template:Template )
		Local box:Box = New Box
		box.id = idNext
		idNext = idNext + 1
		box.x = x; box.y = y
		box.kind = template.name
		
		For Local setting:Setting = EachIn MapValues( template.settings )
			box.settings.Insert( setting.name, setting.Copy() )
		Next
		
		box.ins = template.ins
		box.outs = template.outs
		box.w = Max( 8 + box.gap * ( box.ins - 1 ), TextWidth( box.kind ) + 6 )
		
		If box.ins > 1
			box.gap = Max( 15, ( box.w - 8 * box.ins ) / ( box.ins - 1 ) + 8 )
		EndIf
		
		Return box
	End Function
	
	Method Execute()
		Local in:Box[ ins ]
		
		For Local wire:Wire = EachIn patch.wires
			If wire.b = Self
				in[ wire.bId ] = wire.a
			EndIf
		Next
		
		ExecuteBox( Self, in )
		MakeSparks( Self )
		done = True
	EndMethod
	
	Method Render()
		SetColor 112, 146, 190
		
		If boxSelected = Self
			SetColor 255, 255, 255
		EndIf
		
		DrawRect x, y, w, h
		SetColor 0, 0, 0
		DrawRect x + 1, y + 1, w - 2, h - 2
		
		If IsClickable()
			SetColor 112, 146, 190
			DrawRect x + 1, y + 4, w - 2, h - 8
		EndIf
		
		SetColor 238, 221, 238
		
		For Local n:Int = 0 Until ins
			DrawRect x + n * gap, y, 8, 3
		Next
		
		For Local n:Int = 0 Until outs
			DrawRect x + n * 15, y + h - 3, 8, 3
		Next
		
		If ViewBox( Self ) = Null
			SetColor 255, 255, 255
			
			If Not implementedTemplates.Contains( kind )
				SetColor 255, 0, 0
			EndIf
			
			DrawText kind, x + 3, y + 3
		EndIf
	EndMethod
EndType



Function MakeViewBox:ViewBox( x:Int, y:Int )
		Local view:ViewBox = New ViewBox
		
		view.x = x; view.y = y
		view.w = 84; view.h = 64
		view.kind = "view"
		view.ins = 1
		view.outs = 0
		Return view
End Function



Type ViewBox Extends Box
	Method Render()
		Super.Render()
		SetColor 255, 255, 255
		
		For Local _x:Int = 0 Until 20
		For Local _y:Int = 0 Until 15
			If state[ _x, _y ] = 1
				DrawRect 2 + x + _x * 4, 2 + y + _y * 4, 4, 4
			EndIf
		Next
		Next
	End Method
End Type